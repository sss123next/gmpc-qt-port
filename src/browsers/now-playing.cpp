/**
 * @file now-playing.cpp
 * @brief Full-page «Now Playing» screen — large cover art, song lyrics,
 *        and tabbed metadata panels.
 */

#include "now-playing.h"
#include "widgets/cover-widget.h"
#include "widgets/cover-viewer.h"
#include "metadata/metadata-manager.h"
#include "core/config.h"
#include "core/connection.h"
#include "core/playlist-model.h"

#include <QFileInfo>
#include <QGridLayout>
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

/// Обновить текст строки формы; если @p text пуст — строку прячем.
void NowPlaying::setFormField(QLabel *label, const QString &text)
{
    if (!label) return;
    const bool visible = !text.isEmpty();
    if (visible)
        label->setText(text);
    // Прячем и сам label значения, и его заголовок (QFormLayout создаёт
    // отдельный QLabel для метки строки).
    if (auto *form = qobject_cast<QFormLayout*>(label->parentWidget()->layout())) {
        if (auto *header = form->labelForField(label))
            header->setVisible(visible);
    }
    label->setVisible(visible);
}

/// Создать строку «подпись: значение» в форме и вернуть QLabel значения.
QLabel *NowPlaying::addFormField(QFormLayout *form, const QString &label)
{
    auto *vl = new QLabel;
    vl->setWordWrap(true);
    vl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(label + QStringLiteral(":"), vl);
    return vl;
}

// ── Constructor ─────────────────────────────────────────────────────────────

NowPlaying::NowPlaying(QWidget *parent)
    : BrowserWidget(parent)
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
    artistLabel_->setObjectName(QStringLiteral("artistLabel"));
    titleLabel_ = new QLabel(this);
    titleLabel_->setObjectName(QStringLiteral("titleLabel"));
    albumLabel_ = new QLabel(this);
    albumLabel_->setObjectName(QStringLiteral("albumLabel"));
    // Artist is primary, title secondary, album tertiary — matches SongInfo order.
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
    tabs_->setVisible(false);  // shown only when there is extra data
    lay->addWidget(tabs_);

    // ── Область текста песни ───────────────────────────────────────────
    // Контейнер с текстом и кнопкой обновления поверх.
    auto *lyricsContainer = new QWidget(this);
    auto *lyricsGrid = new QGridLayout(lyricsContainer);
    lyricsGrid->setContentsMargins(0, 0, 0, 0);
    lyricsGrid->setSpacing(0);

    lyricsView_ = new QPlainTextEdit(this);
    lyricsView_->setObjectName(QStringLiteral("lyricsView"));
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
    hintLabel_->setObjectName(QStringLiteral("hintLabel"));
    hintLabel_->setStyleSheet(QStringLiteral("color: palette(disabled, text);"));
    lay->addWidget(hintLabel_);

    showNothing();

    // Click on cover → full-screen viewer (single instance, reused).
    connect(cover_, &CoverWidget::clicked, this, [this] {
        if (cover_->coverData().isEmpty())
            return;
        if (!coverViewer_)
            coverViewer_ = new CoverViewer();
        coverViewer_->show(cover_->coverData());
    });

    // Refresh buttons.
    connect(refreshCoverBtn_, &QPushButton::clicked,
            this, &NowPlaying::onRefreshCover);
    connect(refreshLyricsBtn_, &QPushButton::clicked,
            this, &NowPlaying::onRefreshLyrics);
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

    curArtist_ = song->artist;
    curTitle_  = name;
    curAlbum_  = song->album;
    curUri_    = song->uri;
    lastSongId_ = song->id;

    // Чужой текст не должен пережить смену трэка, даже если страница скрыта:
    // при возврате он выглядел бы лирикой нового.
    lyricsView_->clear();
    hintLabel_->setText(tr("No lyrics"));

    // Free full-screen cover if viewer was hidden (saves ~8 MB per track).
    if (coverViewer_ && !coverViewer_->isVisible())
        coverViewer_->clear();

    // Если страница скрыта — не грузим обложку/табы/лирику: это сделает
    // onActivate(), если ID трэка отличается от lastSongId_.
    if (!isVisible()) {
        needCover_ = true;
        return;
    }

    // Fetch cover only when the page is visible.
    if (curArtist_.isEmpty() || curAlbum_.isEmpty())
        cover_->clear();
    else
        cover_->fetchCover(curArtist_, curAlbum_, curUri_);
    needCover_ = false;

    refreshMetadata();
    requestLyricsForCurrent();
}

// ── Активация страницы (пользователь переключился на Now Playing) ───────────

void NowPlaying::onActivate()
{
    if (curArtist_.isEmpty() && curAlbum_.isEmpty()) {
        if (titleLabel_->text().isEmpty())
            return;
        showNothing();
        return;
    }

    // Если обновление контента было отложено (страница была скрыта, когда
    // пришёл setSong), — синхронизируем всё принудительно.
    if (needCover_) {
        needCover_ = false;
        if (!curArtist_.isEmpty() && !curAlbum_.isEmpty())
            cover_->fetchCover(curArtist_, curAlbum_, curUri_);
        else
            cover_->clear();
        refreshMetadata();
        requestLyricsForCurrent();
        return;
    }

    // Быстро сверяемся с сервером: не сменился ли трэк, пока мы были на
    // другой вкладке (страница была видима при смене — контент не
    // пропускали).
    if (conn_ && conn_->isConnected()) {
        Connection::ResponseScope scope(*conn_, "status");
        mpd_status *status = mpd_run_status(conn_->raw());
        const int curId = status ? mpd_status_get_song_id(status) : -1;
        if (status) mpd_status_free(status);

        if (curId != lastSongId_ && lastSongId_ >= 0) {
            // Трэк сменился — перезагружаем всё.
            if (curArtist_.isEmpty() || curAlbum_.isEmpty())
                cover_->clear();
            else
                cover_->fetchCover(curArtist_, curAlbum_, curUri_);
            refreshMetadata();
            requestLyricsForCurrent();
        }
        return;
    }

    // Нет соединения — ничего не делаем.
}

void NowPlaying::onDeactivate()
{
    if (Config::instance().lowMemoryMode()) {
        // In Low Memory Mode, free heavyweight resources immediately.
        // Сбрасываем lastSongId_ — при возврате onActivate() увидит
        // расхождение и перезагрузит всё заново.
        lastSongId_ = -2;
        needCover_ = true;
        cover_->clear();
        lyricsView_->clear();
        clearTabs();
        hintLabel_->setText(tr("Low Memory Mode — data freed"));
        if (coverViewer_ && coverViewer_->isVisible())
            coverViewer_->clear();
    }
    // Normal режим — ничего не делаем: ресурсы не тратим на скрытой странице.
}

/// Запросить лирику для текущего трэка.
void NowPlaying::requestLyricsForCurrent()
{
    lyricsView_->clear();
    if (!mgr_ || curArtist_.isEmpty()) {
        hintLabel_->setText(tr("No lyrics"));
        return;
    }
    hintLabel_->setText(tr("Loading lyrics\u2026"));
    mgr_->requestLyrics(curArtist_, curTitle_, curUri_);
}

// ── Построение вкладок метаданных (один раз) ────────────────────────────────

void NowPlaying::buildTabs()
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

// ── Metadata refresh (fetch full tags + status from MPD) ────────────────────

void NowPlaying::refreshMetadata()
{
    const QString prevTab = tabs_->currentIndex() >= 0
        ? tabs_->tabText(tabs_->currentIndex()) : QString();

    // Build tabs once
    buildTabs();

    // Скрываем все вкладки — покажем только те, где есть данные.
    tabs_->setVisible(false);
    if (!conn_ || !conn_->isConnected() || !conn_->hasHandle())
        return;

    // ---- Fetch current song from MPD ----
    struct mpd_song *mpd = nullptr;
    {
        Connection::ResponseScope scope(*conn_, "currentsong");
        mpd = mpd_run_current_song(conn_->raw());
    }
    if (!mpd) {
        // Ничего не играет — чистим табы от прошлого трэка.
        clearTabs();
        return;
    }

    // Collect tags.
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

    // ---- Fetch status for audio format & bitrate ----
    uint32_t sampleRate = 0;
    uint8_t  bits       = 0;
    uint8_t  channels   = 0;
    unsigned bitrate    = 0;
    {
        Connection::ResponseScope scope(*conn_, "status");
        mpd_status *status = mpd_run_status(conn_->raw());
        if (status) {
            bitrate = mpd_status_get_kbit_rate(status);
            const mpd_audio_format *af = mpd_status_get_audio_format(status);
            if (af) {
                sampleRate = af->sample_rate;
                bits       = af->bits;
                channels   = af->channels;
            }
            mpd_status_free(status);
        }
    }

    const QString durStr = dur > 0
        ? QStringLiteral("%1:%2").arg(dur / 60).arg(dur % 60, 2, 10, QLatin1Char('0'))
        : QString();
    const QString ext = curUri_.isEmpty() ? QString()
        : QFileInfo(curUri_).suffix().toUpper();

    // ── Обновляем текст строк, пустые прячем ─────────────────────────
    setFormField(detTrack_,       track);
    setFormField(detDisc_,        disc);
    setFormField(detYear_,        date);
    // Original date: показываем только если отличается от года
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

    setFormField(techSampleRate_, sampleRate > 0
        ? QStringLiteral("%1 Hz").arg(sampleRate) : QString());
    if (bits > 0) {
        QString bitStr;
        if (bits == MPD_SAMPLE_FORMAT_FLOAT)
            bitStr = QStringLiteral("Float");
        else if (bits == MPD_SAMPLE_FORMAT_DSD)
            bitStr = QStringLiteral("DSD");
        else
            bitStr = QStringLiteral("%1 bit").arg(bits);
        setFormField(techBitDepth_, bitStr);
    } else {
        setFormField(techBitDepth_, QString());
    }
    setFormField(techChannels_, channels > 0 ? QString::number(channels) : QString());
    setFormField(techBitrate_,  bitrate > 0 ? QStringLiteral("%1 kbps").arg(bitrate) : QString());
    setFormField(techFormat_,   ext);
    setFormField(techFile_,     curUri_);

    // Показываем вкладки, только если хоть одна строка непустая
    tabs_->setVisible(true);

    // Restore previously selected tab
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

// ── Refresh buttons ─────────────────────────────────────────────────────────

void NowPlaying::onRefreshCover()
{
    if (!mgr_ || curArtist_.isEmpty() || curAlbum_.isEmpty())
        return;
    // Clear current display and force a fresh fetch through the full pipeline.
    cover_->clear();
    mgr_->refreshCover(curArtist_, curAlbum_, curUri_);
}

void NowPlaying::onRefreshLyrics()
{
    if (!mgr_ || curArtist_.isEmpty())
        return;
    lyricsView_->clear();
    hintLabel_->setText(tr("Refreshing lyrics\u2026"));
    mgr_->refreshLyrics(curArtist_, curTitle_, curUri_);
}

// ── Helpers ─────────────────────────────────────────────────────────────────

QString NowPlaying::lyricsKey(const QString &artist, const QString &title)
{
    return artist + QLatin1Char('\n') + title;
}

/// Очистить текст и скрыть все строки вкладок метаданных.
void NowPlaying::clearTabs()
{
    if (!tabsBuilt_) return;

    auto clearField = [](QLabel *lbl) {
        if (lbl) { lbl->clear(); lbl->setVisible(false); }
    };

    // Details
    clearField(detTrack_); clearField(detDisc_);
    clearField(detYear_); clearField(detOrigDate_); clearField(detGenre_);
    clearField(detAlbumArtist_); clearField(detDuration_);

    // Credits
    clearField(credComposer_); clearField(credPerformer_);
    clearField(credConductor_); clearField(credEnsemble_); clearField(credGrouping_);
    clearField(credMood_); clearField(credLocation_); clearField(credLabel_);
    clearField(credWork_); clearField(credMovement_); clearField(credMovNum_);

    // IDs & Sort
    clearField(idTitleSort_); clearField(idArtistSort_);
    clearField(idAlbumSort_); clearField(idAlbumArtSort_); clearField(idCompSort_);
    clearField(idComment_);
    clearField(idMbArtist_); clearField(idMbAlbum_); clearField(idMbAlbumArt_);
    clearField(idMbTrack_); clearField(idMbWork_); clearField(idMbRelTrack_);
    clearField(idMbRelGroup_);

    // Technical
    clearField(techSampleRate_); clearField(techBitDepth_);
    clearField(techChannels_); clearField(techBitrate_);
    clearField(techFormat_); clearField(techFile_);

    tabs_->setVisible(false);
}

void NowPlaying::showNothing()
{
    cover_->clear();
    titleLabel_->clear();
    artistLabel_->clear();
    albumLabel_->clear();
    lyricsView_->clear();
    clearTabs();
    hintLabel_->setText(tr("Nothing is playing"));
    curArtist_.clear();
    curTitle_.clear();
    curAlbum_.clear();
    curUri_.clear();
    lastSongId_ = -2;
    needCover_ = false;
    if (coverViewer_)
        coverViewer_->clear();
}

void NowPlaying::onConnectionChanged(bool connected)
{
    BrowserWidget::onConnectionChanged(connected);
    if (!connected) {
        showNothing();
    }
}
