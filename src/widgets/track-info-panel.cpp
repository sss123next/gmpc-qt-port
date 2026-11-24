/**
 * @file track-info-panel.cpp
 * @brief Переиспользуемая панель информации о трэке.
 *
 * Визуал повторяет Now Playing: крупная обложка слева, теги и табы метаданных
 * справа, лирика снизу. Работает с любым трэком по URI (не только с текущим).
 */

#include "track-info-panel.h"
#include "widgets/cover-widget.h"
#include "widgets/cover-viewer.h"
#include "metadata/metadata-manager.h"
#include "core/connection.h"

#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QTabWidget>
#include <QVBoxLayout>
#include <mpd/client.h>

// ── Static helpers ──────────────────────────────────────────────────────────

/// Safely read a single MPD tag value (first instance).
static QString tag(const mpd_song *song, mpd_tag_type type)
{
    const char *v = mpd_song_get_tag(song, type, 0);
    return v ? QString::fromUtf8(v) : QString();
}

/// Обновить текст строки формы; если @p text пуст — строку прячем.
void TrackInfoPanel::setFormField(QLabel *label, const QString &text)
{
    if (!label) return;
    const bool visible = !text.isEmpty();
    if (visible)
        label->setText(text);
    if (auto *form = qobject_cast<QFormLayout*>(label->parentWidget()->layout())) {
        if (auto *header = form->labelForField(label))
            header->setVisible(visible);
    }
    label->setVisible(visible);
}

/// Создать строку «подпись: значение» в форме и вернуть QLabel значения.
QLabel *TrackInfoPanel::addFormField(QFormLayout *form, const QString &label)
{
    auto *vl = new QLabel;
    vl->setWordWrap(true);
    vl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(label + QStringLiteral(":"), vl);
    return vl;
}

// ── Construction ────────────────────────────────────────────────────────────

TrackInfoPanel::TrackInfoPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 12, 12, 12);

    // ── Верх: крупная обложка слева, теги справа ──────────────────────
    auto *topRow = new QHBoxLayout;

    auto *coverContainer = new QWidget(this);
    auto *coverGrid = new QGridLayout(coverContainer);
    coverGrid->setContentsMargins(0, 0, 0, 0);
    coverGrid->setSpacing(0);

    cover_ = new CoverWidget(this);
    cover_->setCoverSize(256);
    coverGrid->addWidget(cover_, 0, 0);

    refreshCoverBtn_ = new QPushButton(this);
    refreshCoverBtn_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    refreshCoverBtn_->setFixedSize(24, 24);
    refreshCoverBtn_->setFlat(true);
    refreshCoverBtn_->setCursor(Qt::PointingHandCursor);
    refreshCoverBtn_->setToolTip(tr("Refresh cover"));
    coverGrid->addWidget(refreshCoverBtn_, 0, 0, Qt::AlignBottom | Qt::AlignRight);

    topRow->addWidget(coverContainer, 0, Qt::AlignTop);

    auto *infoCol = new QVBoxLayout;
    artistLabel_ = new QLabel(this);
    artistLabel_->setObjectName(QStringLiteral("tpiArtistLabel"));
    titleLabel_ = new QLabel(this);
    titleLabel_->setObjectName(QStringLiteral("tpiTitleLabel"));
    albumLabel_ = new QLabel(this);
    albumLabel_->setObjectName(QStringLiteral("tpiAlbumLabel"));
    artistLabel_->setStyleSheet(QStringLiteral("font-size: 15pt; font-weight: bold;"));
    titleLabel_->setStyleSheet(QStringLiteral("font-size: 13pt;"));
    albumLabel_->setStyleSheet(QStringLiteral("color: palette(disabled, windowText);"));
    for (QLabel *l : {artistLabel_, titleLabel_, albumLabel_}) {
        l->setWordWrap(true);
        l->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        infoCol->addWidget(l);
    }
    infoCol->addStretch(1);
    topRow->addLayout(infoCol, 1);
    lay->addLayout(topRow);

    // ── Вкладки с метаданными ──────────────────────────────────────────
    tabs_ = new QTabWidget(this);
    tabs_->setTabPosition(QTabWidget::South);
    tabs_->setVisible(false);
    lay->addWidget(tabs_);

    // ── Область текста песни ───────────────────────────────────────────
    auto *lyricsContainer = new QWidget(this);
    auto *lyricsGrid = new QGridLayout(lyricsContainer);
    lyricsGrid->setContentsMargins(0, 0, 0, 0);
    lyricsGrid->setSpacing(0);

    lyricsView_ = new QPlainTextEdit(this);
    lyricsView_->setObjectName(QStringLiteral("tpiLyricsView"));
    lyricsView_->setReadOnly(true);
    lyricsView_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    lyricsGrid->addWidget(lyricsView_, 0, 0);

    refreshLyricsBtn_ = new QPushButton(this);
    refreshLyricsBtn_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    refreshLyricsBtn_->setFixedSize(24, 24);
    refreshLyricsBtn_->setFlat(true);
    refreshLyricsBtn_->setCursor(Qt::PointingHandCursor);
    refreshLyricsBtn_->setToolTip(tr("Refresh lyrics"));
    lyricsGrid->addWidget(refreshLyricsBtn_, 0, 0, Qt::AlignTop | Qt::AlignRight);

    lay->addWidget(lyricsContainer, 1);

    hintLabel_ = new QLabel(this);
    hintLabel_->setObjectName(QStringLiteral("tpiHintLabel"));
    hintLabel_->setStyleSheet(QStringLiteral("color: palette(disabled, text);"));
    lay->addWidget(hintLabel_);

    clear();

    // Click on cover → full-screen viewer.
    connect(cover_, &CoverWidget::clicked, this, [this] {
        if (cover_->coverData().isEmpty())
            return;
        if (!coverViewer_)
            coverViewer_ = new CoverViewer();
        coverViewer_->show(cover_->coverData());
        emit coverClicked();
    });

    connect(refreshCoverBtn_, &QPushButton::clicked,
            this, &TrackInfoPanel::onRefreshCover);
    connect(refreshLyricsBtn_, &QPushButton::clicked,
            this, &TrackInfoPanel::onRefreshLyrics);
}

TrackInfoPanel::~TrackInfoPanel()
{
    delete coverViewer_;
}

// ── Setters ─────────────────────────────────────────────────────────────────

void TrackInfoPanel::setConnection(Connection *conn)
{
    conn_ = conn;
}

void TrackInfoPanel::setMetadataManager(MetadataManager *mgr)
{
    mgr_ = mgr;
    if (!mgr_)
        return;

    cover_->setMetadataManager(mgr_);

    connect(mgr_, &MetadataManager::lyricsReady,
            this, &TrackInfoPanel::onLyricsReady);
    connect(mgr_, &MetadataManager::lyricsNotFound,
            this, &TrackInfoPanel::onLyricsNotFound);
}

// ── Show / Clear ────────────────────────────────────────────────────────────

void TrackInfoPanel::showTrack(const SongEntry &song)
{
    const QString name = song.title.isEmpty()
                             ? QFileInfo(song.uri).fileName()
                             : song.title;

    titleLabel_->setText(name);
    artistLabel_->setText(song.artist);
    albumLabel_->setText(song.album);

    curArtist_ = song.artist;
    curTitle_  = name;
    curAlbum_  = song.album;
    curUri_    = song.uri;

    lyricsView_->clear();
    hintLabel_->setText(tr("No lyrics"));

    if (coverViewer_ && !coverViewer_->isVisible())
        coverViewer_->clear();

    // Cover
    if (curArtist_.isEmpty() || curAlbum_.isEmpty())
        cover_->clear();
    else
        cover_->fetchCover(curArtist_, curAlbum_, curUri_);

    // Metadata tabs (full MPD tags by URI)
    fetchMetadata(song.uri);

    // Lyrics
    requestLyricsForCurrent();
}

void TrackInfoPanel::clear()
{
    cover_->clear();
    titleLabel_->clear();
    artistLabel_->clear();
    albumLabel_->clear();
    lyricsView_->clear();
    clearTabs();
    hintLabel_->setText(tr("Nothing selected"));
    curArtist_.clear();
    curTitle_.clear();
    curAlbum_.clear();
    curUri_.clear();
    if (coverViewer_)
        coverViewer_->clear();
}

// ── Fetch metadata by URI (not from current song) ──────────────────────────

void TrackInfoPanel::fetchMetadata(const QString &uri)
{
    const QString prevTab = tabs_->currentIndex() >= 0
        ? tabs_->tabText(tabs_->currentIndex()) : QString();

    buildTabs();
    tabs_->setVisible(false);

    if (!conn_ || !conn_->isConnected() || !conn_->hasHandle() || uri.isEmpty())
        return;

    // ---- Fetch song by URI from MPD database ----
    struct mpd_song *mpd = nullptr;
    {
        const QByteArray u = uri.toUtf8();
        Connection::ResponseScope scope(*conn_, "search");

        if (!mpd_search_db_songs(conn_->raw(), true))
            return;

        bool ok = mpd_search_add_uri_constraint(conn_->raw(),
                    MPD_OPERATOR_DEFAULT, u.constData());
        ok = ok && mpd_search_commit(conn_->raw());
        if (!ok) {
            mpd_search_cancel(conn_->raw());
            return;
        }

        // Take the first (and only) match
        mpd = mpd_recv_song(conn_->raw());
        // Drain any remaining to clear the response
        while (mpd_song *s = mpd_recv_song(conn_->raw()))
            mpd_song_free(s);
    }
    if (!mpd) return;

    // Read all interesting tags
    const QString track      = tag(mpd, MPD_TAG_TRACK);
    const QString disc       = tag(mpd, MPD_TAG_DISC);
    const QString date       = tag(mpd, MPD_TAG_DATE);
    const QString genre      = tag(mpd, MPD_TAG_GENRE);
    const QString composer   = tag(mpd, MPD_TAG_COMPOSER);
    const QString perform    = tag(mpd, MPD_TAG_PERFORMER);
    const QString label      = tag(mpd, MPD_TAG_LABEL);
    const QString comment    = tag(mpd, MPD_TAG_COMMENT);
    const QString cond       = tag(mpd, MPD_TAG_CONDUCTOR);
    const QString ens        = tag(mpd, MPD_TAG_ENSEMBLE);
    const QString mbArtist   = tag(mpd, MPD_TAG_MUSICBRAINZ_ARTISTID);
    const QString mbAlbum    = tag(mpd, MPD_TAG_MUSICBRAINZ_ALBUMID);
    const QString mbTrack    = tag(mpd, MPD_TAG_MUSICBRAINZ_TRACKID);
    const QString origDate   = tag(mpd, MPD_TAG_ORIGINAL_DATE);
    const QString asort      = tag(mpd, MPD_TAG_ARTIST_SORT);
    const QString alsort     = tag(mpd, MPD_TAG_ALBUM_SORT);
    const QString work       = tag(mpd, MPD_TAG_WORK);
    const QString mov        = tag(mpd, MPD_TAG_MOVEMENT);
    const QString movNum     = tag(mpd, MPD_TAG_MOVEMENTNUMBER);

    const QString albumArtist  = tag(mpd, MPD_TAG_ALBUM_ARTIST);
    const QString grouping     = tag(mpd, MPD_TAG_GROUPING);
    const QString mood         = tag(mpd, MPD_TAG_MOOD);
    const QString location     = tag(mpd, MPD_TAG_LOCATION);
    const QString titleSort    = tag(mpd, MPD_TAG_TITLE_SORT);
    const QString compSort     = tag(mpd, MPD_TAG_COMPOSER_SORT);
    const QString alsortArt    = tag(mpd, MPD_TAG_ALBUM_ARTIST_SORT);
    const QString mbAlbumArt   = tag(mpd, MPD_TAG_MUSICBRAINZ_ALBUMARTISTID);
    const QString mbWork       = tag(mpd, MPD_TAG_MUSICBRAINZ_WORKID);
    const QString mbRelTrack   = tag(mpd, MPD_TAG_MUSICBRAINZ_RELEASETRACKID);
    const QString mbRelGroup   = tag(mpd, MPD_TAG_MUSICBRAINZ_RELEASEGROUPID);

    const int dur = static_cast<int>(mpd_song_get_duration(mpd));
    mpd_song_free(mpd);

    const QString durStr = dur > 0
        ? QStringLiteral("%1:%2").arg(dur / 60).arg(dur % 60, 2, 10, QLatin1Char('0'))
        : QString();
    const QString ext = uri.isEmpty() ? QString()
        : QFileInfo(uri).suffix().toUpper();

    // Audio format data (sample rate, bit depth, channels, bitrate) доступны
    // только из mpd_status — то есть только для текущего трэка. Для выбранного
    // трэка эти поля остаются пустыми.

    // ── Fill tab fields ─────────────────────────────────────────────────
    setFormField(detTrack_,       track);
    setFormField(detDisc_,        disc);
    setFormField(detYear_,        date);
    setFormField(detOrigDate_,    (!origDate.isEmpty() && origDate != date) ? origDate : QString());
    setFormField(detGenre_,       genre);
    setFormField(detAlbumArtist_, albumArtist);
    setFormField(detDuration_,    durStr);

    setFormField(credComposer_,  composer);
    setFormField(credPerformer_, perform);
    setFormField(credConductor_, cond);
    setFormField(credEnsemble_,  ens);
    setFormField(credGrouping_,  grouping);
    setFormField(credMood_,      mood);
    setFormField(credLocation_,  location);
    setFormField(credLabel_,     label);
    setFormField(credWork_,      work);
    setFormField(credMovement_,  mov);
    setFormField(credMovNum_,    movNum);

    setFormField(idTitleSort_,     titleSort);
    setFormField(idArtistSort_,    asort);
    setFormField(idAlbumSort_,     alsort);
    setFormField(idAlbumArtSort_,  alsortArt);
    setFormField(idCompSort_,      compSort);
    setFormField(idComment_,       comment);
    setFormField(idMbArtist_,      mbArtist);
    setFormField(idMbAlbum_,       mbAlbum);
    setFormField(idMbAlbumArt_,    mbAlbumArt);
    setFormField(idMbTrack_,       mbTrack);
    setFormField(idMbWork_,        mbWork);
    setFormField(idMbRelTrack_,    mbRelTrack);
    setFormField(idMbRelGroup_,    mbRelGroup);

    // Technical tab: only format and file (no audio stream data)
    setFormField(techSampleRate_, QString());
    setFormField(techBitDepth_,   QString());
    setFormField(techChannels_,   QString());
    setFormField(techBitrate_,    QString());
    setFormField(techFormat_,     ext);
    setFormField(techFile_,       uri);

    tabs_->setVisible(true);

    int restoreIdx = 0;
    if (!prevTab.isEmpty()) {
        for (int i = 0; i < tabs_->count(); ++i) {
            if (tabs_->tabText(i) == prevTab) {
                restoreIdx = i;
                break;
            }
        }
    }
    tabs_->setCurrentIndex(restoreIdx);
}

// ── Lyrics integration ──────────────────────────────────────────────────────

void TrackInfoPanel::requestLyricsForCurrent()
{
    lyricsView_->clear();
    if (!mgr_ || curArtist_.isEmpty()) {
        hintLabel_->setText(tr("No lyrics"));
        return;
    }
    hintLabel_->setText(tr("Loading lyrics\u2026"));
    mgr_->requestLyrics(curArtist_, curTitle_, curUri_);
}

QString TrackInfoPanel::lyricsKey(const QString &artist, const QString &title)
{
    return artist + QLatin1Char('\n') + title;
}

void TrackInfoPanel::onLyricsReady(const QString &artist, const QString &title,
                                    const QString &lyrics)
{
    if (lyricsKey(artist, title) != lyricsKey(curArtist_, curTitle_))
        return;
    lyricsView_->setPlainText(lyrics);
    hintLabel_->clear();
}

void TrackInfoPanel::onLyricsNotFound(const QString &artist, const QString &title)
{
    if (lyricsKey(artist, title) != lyricsKey(curArtist_, curTitle_))
        return;
    lyricsView_->setPlainText({});
    hintLabel_->setText(tr("No lyrics found"));
}

// ── Refresh buttons ─────────────────────────────────────────────────────────

void TrackInfoPanel::onRefreshCover()
{
    if (!mgr_ || curArtist_.isEmpty() || curAlbum_.isEmpty())
        return;
    cover_->clear();
    mgr_->refreshCover(curArtist_, curAlbum_, curUri_);
}

void TrackInfoPanel::onRefreshLyrics()
{
    if (!mgr_ || curArtist_.isEmpty())
        return;
    lyricsView_->clear();
    hintLabel_->setText(tr("Refreshing lyrics\u2026"));
    mgr_->refreshLyrics(curArtist_, curTitle_, curUri_);
}

// ── Metadata tabs (built once, reused) ──────────────────────────────────────

void TrackInfoPanel::buildTabs()
{
    if (tabsBuilt_) return;
    tabsBuilt_ = true;

    auto mkTab = [&](const QString &title) {
        auto *w = new QWidget;
        auto *f = new QFormLayout(w);
        f->setContentsMargins(8, 8, 8, 8);
        tabs_->addTab(w, title);
        return f;
    };

    // ── Tab: Details ──
    {
        auto *f = mkTab(tr("Details"));
        detTrack_       = addFormField(f, tr("Track"));
        detDisc_        = addFormField(f, tr("Disc"));
        detYear_        = addFormField(f, tr("Year"));
        detOrigDate_    = addFormField(f, tr("Original date"));
        detGenre_       = addFormField(f, tr("Genre"));
        detAlbumArtist_ = addFormField(f, tr("Album artist"));
        detDuration_    = addFormField(f, tr("Duration"));
    }

    // ── Tab: Credits ──
    {
        auto *f = mkTab(tr("Credits"));
        credComposer_  = addFormField(f, tr("Composer"));
        credPerformer_ = addFormField(f, tr("Performer"));
        credConductor_ = addFormField(f, tr("Conductor"));
        credEnsemble_  = addFormField(f, tr("Ensemble"));
        credGrouping_  = addFormField(f, tr("Grouping"));
        credMood_      = addFormField(f, tr("Mood"));
        credLocation_  = addFormField(f, tr("Location"));
        credLabel_     = addFormField(f, tr("Label"));
        credWork_      = addFormField(f, tr("Work"));
        credMovement_  = addFormField(f, tr("Movement"));
        credMovNum_    = addFormField(f, tr("Movement No."));
    }

    // ── Tab: IDs & Sort ──
    {
        auto *f = mkTab(tr("IDs & Sort"));
        idTitleSort_     = addFormField(f, tr("Title sort"));
        idArtistSort_    = addFormField(f, tr("Artist sort"));
        idAlbumSort_     = addFormField(f, tr("Album sort"));
        idAlbumArtSort_  = addFormField(f, tr("Album artist sort"));
        idCompSort_      = addFormField(f, tr("Composer sort"));
        idComment_       = addFormField(f, tr("Comment"));
        idMbArtist_      = addFormField(f, tr("MB Artist ID"));
        idMbAlbum_       = addFormField(f, tr("MB Album ID"));
        idMbAlbumArt_    = addFormField(f, tr("MB Album Artist ID"));
        idMbTrack_       = addFormField(f, tr("MB Track ID"));
        idMbWork_        = addFormField(f, tr("MB Work ID"));
        idMbRelTrack_    = addFormField(f, tr("MB Release Track ID"));
        idMbRelGroup_    = addFormField(f, tr("MB Release Group ID"));
    }

    // ── Tab: Technical ──
    {
        auto *f = mkTab(tr("Technical"));
        techSampleRate_ = addFormField(f, tr("Sample rate"));
        techBitDepth_   = addFormField(f, tr("Bit depth"));
        techChannels_   = addFormField(f, tr("Channels"));
        techBitrate_    = addFormField(f, tr("Bitrate"));
        techFormat_     = addFormField(f, tr("Format"));
        techFile_       = addFormField(f, tr("File"));
    }
}

void TrackInfoPanel::clearTabs()
{
    if (!tabsBuilt_) return;

    auto clearField = [](QLabel *lbl) {
        if (lbl) { lbl->clear(); lbl->setVisible(false); }
    };

    clearField(detTrack_); clearField(detDisc_);
    clearField(detYear_); clearField(detOrigDate_); clearField(detGenre_);
    clearField(detAlbumArtist_); clearField(detDuration_);

    clearField(credComposer_); clearField(credPerformer_);
    clearField(credConductor_); clearField(credEnsemble_); clearField(credGrouping_);
    clearField(credMood_); clearField(credLocation_); clearField(credLabel_);
    clearField(credWork_); clearField(credMovement_); clearField(credMovNum_);

    clearField(idTitleSort_); clearField(idArtistSort_);
    clearField(idAlbumSort_); clearField(idAlbumArtSort_); clearField(idCompSort_);
    clearField(idComment_);
    clearField(idMbArtist_); clearField(idMbAlbum_); clearField(idMbAlbumArt_);
    clearField(idMbTrack_); clearField(idMbWork_); clearField(idMbRelTrack_);
    clearField(idMbRelGroup_);

    clearField(techSampleRate_); clearField(techBitDepth_);
    clearField(techChannels_); clearField(techBitrate_);
    clearField(techFormat_); clearField(techFile_);

    tabs_->setVisible(false);
}
