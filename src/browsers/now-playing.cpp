/**
 * @file now-playing.cpp
 * @brief Full-page «Now Playing» screen — large cover art, song lyrics,
 *        and tabbed metadata panels.
 */

#include "now-playing.h"
#include "widgets/cover-widget.h"
#include "widgets/cover-viewer.h"
#include "metadata/metadata-manager.h"
#include "core/connection.h"
#include "core/playlist-model.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QScrollArea>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QString>
#include <mpd/client.h>

// ── Helpers ─────────────────────────────────────────────────────────────────

/// Safely read a single MPD tag value (first instance).
static QString tag(const mpd_song *song, mpd_tag_type type)
{
    const char *v = mpd_song_get_tag(song, type, 0);
    return v ? QString::fromUtf8(v) : QString();
}

/// Build a «Key: Value» label for a tab.
static QLabel *kvLabel(const QString &val)
{
    if (val.isEmpty()) return nullptr;
    auto *l = new QLabel(val);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

/// Add a row to a QFormLayout if @p value is non-empty.
static void addRow(QFormLayout *form, const QString &label, const QString &value)
{
    if (!value.isEmpty())
        form->addRow(label + QStringLiteral(":"), new QLabel(value));
}

// ── Constructor ─────────────────────────────────────────────────────────────

NowPlaying::NowPlaying(QWidget *parent)
    : BrowserWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 12, 12, 12);

    // ── Верх: крупная обложка слева, теги справа ──────────────────────
    auto *topRow = new QHBoxLayout;
    cover_ = new CoverWidget(this);
    cover_->setCoverSize(256);
    topRow->addWidget(cover_, 0, Qt::AlignTop);

    auto *infoCol = new QVBoxLayout;
    titleLabel_ = new QLabel(this);
    titleLabel_->setObjectName(QStringLiteral("titleLabel"));
    artistLabel_ = new QLabel(this);
    artistLabel_->setObjectName(QStringLiteral("artistLabel"));
    albumLabel_ = new QLabel(this);
    albumLabel_->setObjectName(QStringLiteral("albumLabel"));
    titleLabel_->setStyleSheet(QStringLiteral("font-size: 20pt; font-weight: bold;"));
    artistLabel_->setStyleSheet(QStringLiteral("font-size: 13pt;"));
    for (QLabel *l : {titleLabel_, artistLabel_, albumLabel_}) {
        l->setWordWrap(true);
        l->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        infoCol->addWidget(l);
    }
    topRow->addLayout(infoCol, 1);
    lay->addLayout(topRow);

    // ── Вкладки с метаданными ──────────────────────────────────────────
    tabs_ = new QTabWidget(this);
    tabs_->setTabPosition(QTabWidget::South);
    tabs_->setVisible(false);  // shown only when there is extra data
    lay->addWidget(tabs_);

    // ── Текст песни ────────────────────────────────────────────────────
    lyricsView_ = new QPlainTextEdit(this);
    lyricsView_->setObjectName(QStringLiteral("lyricsView"));
    lyricsView_->setReadOnly(true);
    lyricsView_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    lay->addWidget(lyricsView_, 1);

    hintLabel_ = new QLabel(this);
    hintLabel_->setObjectName(QStringLiteral("hintLabel"));
    hintLabel_->setStyleSheet(QStringLiteral("color: palette(disabled, text);"));
    lay->addWidget(hintLabel_);

    showNothing();

    // Click on cover → full-screen viewer.
    connect(cover_, &CoverWidget::clicked, this, [this] {
        const QPixmap pm = cover_->pixmap();
        if (!pm.isNull()) {
            auto *viewer = new CoverViewer();
            viewer->show(pm);
        }
    });
}

// ── Connection & MetadataManager ────────────────────────────────────────────

void NowPlaying::setConnection(Connection *conn)
{
    conn_ = conn;
}

void NowPlaying::setMetadataManager(MetadataManager *mgr)
{
    mgr_ = mgr;
    if (!mgr_)
        return;

    cover_->setMetadataManager(mgr_);

    connect(mgr_, &MetadataManager::lyricsReady,
            this, &NowPlaying::onLyricsReady);
    connect(mgr_, &MetadataManager::lyricsNotFound,
            this, &NowPlaying::onLyricsNotFound);
}

// ── Song update ─────────────────────────────────────────────────────────────

void NowPlaying::setSong(const SongEntry *song)
{
    if (!song) {
        showNothing();
        return;
    }

    const QString name = song->title.isEmpty()
                             ? QFileInfo(song->uri).fileName()
                             : song->title;

    titleLabel_->setText(name);
    artistLabel_->setText(song->artist);
    albumLabel_->setText(song->album);

    if (song->artist.isEmpty() || song->album.isEmpty())
        cover_->clear();
    else
        cover_->fetchCover(song->artist, song->album, song->uri);

    curArtist_ = song->artist;
    curTitle_  = name;
    curUri_    = song->uri;

    // Refresh tabbed metadata.
    refreshMetadata();

    // Lyrics.
    lyricsView_->clear();
    if (!mgr_ || curArtist_.isEmpty()) {
        hintLabel_->setText(tr("No lyrics"));
        return;
    }
    hintLabel_->setText(tr("Loading lyrics\u2026"));
    mgr_->requestLyrics(curArtist_, curTitle_);
}

// ── Metadata refresh (fetch full tags from MPD) ─────────────────────────────

void NowPlaying::refreshMetadata()
{
    // Clear old tabs.
    tabs_->clear();
    tabs_->setVisible(false);

    if (!conn_ || !conn_->isConnected() || !conn_->hasHandle())
        return;

    // ---- Fetch current song from MPD ----
    Connection::ResponseScope scope(*conn_, "currentsong");
    struct mpd_song *mpd = mpd_run_current_song(conn_->raw());
    if (!mpd)
        return;   // nothing playing or error

    // Collect tags.
    const QString track   = tag(mpd, MPD_TAG_TRACK);
    const QString disc    = tag(mpd, MPD_TAG_DISC);
    const QString date    = tag(mpd, MPD_TAG_DATE);
    const QString genre   = tag(mpd, MPD_TAG_GENRE);
    const QString composer= tag(mpd, MPD_TAG_COMPOSER);
    const QString perform = tag(mpd, MPD_TAG_PERFORMER);
    const QString label   = tag(mpd, MPD_TAG_LABEL);
    const QString comment = tag(mpd, MPD_TAG_COMMENT);
    const QString cond    = tag(mpd, MPD_TAG_CONDUCTOR);
    const QString ens     = tag(mpd, MPD_TAG_ENSEMBLE);
    const QString mbArtist= tag(mpd, MPD_TAG_MUSICBRAINZ_ARTISTID);
    const QString mbAlbum = tag(mpd, MPD_TAG_MUSICBRAINZ_ALBUMID);
    const QString mbTrack = tag(mpd, MPD_TAG_MUSICBRAINZ_TRACKID);
    const QString origDate= tag(mpd, MPD_TAG_ORIGINAL_DATE);
    const QString asort   = tag(mpd, MPD_TAG_ARTIST_SORT);
    const QString alsort  = tag(mpd, MPD_TAG_ALBUM_SORT);
    const QString work    = tag(mpd, MPD_TAG_WORK);
    const QString mov     = tag(mpd, MPD_TAG_MOVEMENT);
    const QString movNum  = tag(mpd, MPD_TAG_MOVEMENTNUMBER);

    const int dur = static_cast<int>(mpd_song_get_duration(mpd));
    mpd_song_free(mpd);

    // Format duration.
    const QString durStr = dur > 0
        ? QStringLiteral("%1:%2")
              .arg(dur / 60).arg(dur % 60, 2, 10, QLatin1Char('0'))
        : QString();

    // ── Tab 1: Details ──────────────────────────────────────────────
    auto *detForm = new QFormLayout;
    detForm->setContentsMargins(8, 8, 8, 8);
    addRow(detForm, tr("Track"),      track);
    addRow(detForm, tr("Disc"),       disc);
    addRow(detForm, tr("Year"),       date);
    if (!origDate.isEmpty() && origDate != date)
        addRow(detForm, tr("Original date"), origDate);
    addRow(detForm, tr("Genre"),      genre);
    addRow(detForm, tr("Duration"),   durStr);
    if (detForm->rowCount() > 0) {
        auto *w = new QWidget;
        w->setLayout(detForm);
        tabs_->addTab(w, tr("Details"));
    } else {
        delete detForm;
    }

    // ── Tab 2: Credits ──────────────────────────────────────────────
    auto *credForm = new QFormLayout;
    credForm->setContentsMargins(8, 8, 8, 8);
    addRow(credForm, tr("Composer"),  composer);
    addRow(credForm, tr("Performer"), perform);
    addRow(credForm, tr("Conductor"), cond);
    addRow(credForm, tr("Ensemble"),  ens);
    addRow(credForm, tr("Label"),     label);
    addRow(credForm, tr("Work"),      work);
    addRow(credForm, tr("Movement"),  mov);
    if (!movNum.isEmpty())
        addRow(credForm, tr("Movement No."), movNum);
    if (credForm->rowCount() > 0) {
        auto *w = new QWidget;
        w->setLayout(credForm);
        tabs_->addTab(w, tr("Credits"));
    } else {
        delete credForm;
    }

    // ── Tab 3: IDs & Sort ───────────────────────────────────────────
    auto *idForm = new QFormLayout;
    idForm->setContentsMargins(8, 8, 8, 8);
    addRow(idForm, tr("Artist sort"), asort);
    addRow(idForm, tr("Album sort"),  alsort);
    addRow(idForm, tr("Comment"),     comment);
    addRow(idForm, tr("MB Artist ID"), mbArtist);
    addRow(idForm, tr("MB Album ID"),  mbAlbum);
    addRow(idForm, tr("MB Track ID"),  mbTrack);
    if (idForm->rowCount() > 0) {
        auto *w = new QWidget;
        w->setLayout(idForm);
        tabs_->addTab(w, tr("IDs & Sort"));
    } else {
        delete idForm;
    }

    tabs_->setVisible(tabs_->count() > 0);
}

// ── Lyrics callbacks ───────────────────────────────────────────────────────

void NowPlaying::onLyricsReady(const QString &artist, const QString &title,
                               const QString &lyrics)
{
    if (lyricsKey(artist, title) != lyricsKey(curArtist_, curTitle_))
        return;
    lyricsView_->setPlainText(lyrics);
    hintLabel_->clear();
}

void NowPlaying::onLyricsNotFound(const QString &artist, const QString &title)
{
    if (lyricsKey(artist, title) != lyricsKey(curArtist_, curTitle_))
        return;
    lyricsView_->setPlainText({});
    hintLabel_->setText(tr("No lyrics found"));
}

// ── Helpers ─────────────────────────────────────────────────────────────────

QString NowPlaying::lyricsKey(const QString &artist, const QString &title)
{
    return artist + QLatin1Char('\n') + title;
}

void NowPlaying::showNothing()
{
    cover_->clear();
    titleLabel_->clear();
    artistLabel_->clear();
    albumLabel_->clear();
    lyricsView_->clear();
    tabs_->clear();
    tabs_->setVisible(false);
    hintLabel_->setText(tr("Nothing is playing"));
    curArtist_.clear();
    curTitle_.clear();
    curUri_.clear();
}

void NowPlaying::onConnectionChanged(bool connected)
{
    BrowserWidget::onConnectionChanged(connected);
    if (!connected) {
        showNothing();
    }
}
