#include "metadata-manager.h"
#include "cover-provider.h"
#include "cache-sqlite.h"
#include "providers/lyrdb.h"
#include "providers/lyrics-ovh.h"
#include "tag-reader.h"
#include "core/config.h"

#include <QFileInfo>
#include <QRegularExpression>

// ── Static helpers ───────────────────────────────────────────────────────────

/// Heuristic: text starts with '<' followed by an ASCII letter → XML.
static bool looksLikeXml(const QString &text)
{
    return text.trimmed().startsWith('<') && text.size() > 1
        && text.trimmed().at(1).isLetter();
}

/// Quick-and-dirty XML tag stripper for backward-compat cache entries.
static QString stripXmlTags(const QString &xml)
{
    QString out = xml;
    out.remove(QRegularExpression("<\\?xml[^>]*\\?>"));
    out.replace(QRegularExpression("<br\\s*/?>"), "\n");
    out.remove(QRegularExpression("<[^>]*>"));
    out.replace("&amp;", "&");
    out.replace("&lt;", "<");
    out.replace("&gt;", ">");
    out.replace("&quot;", "\"");
    out.replace("&apos;", "'");
    out = out.trimmed();
    out.replace(QRegularExpression("\\n{3,}"), "\n\n");
    return out;
}

MetadataManager::MetadataManager(const QString &cachePath,
                                 const QString &lastFmApiKey,
                                 const QString &discogsToken,
                                 const QString &musicDir,
                                 QObject *parent)
    : QObject(parent)
    , coverProvider_(new CoverProvider(lastFmApiKey, discogsToken, cachePath, musicDir, this))
    , lyricsProvider_(new LrcLibProvider(this))
    , lrcRedProvider_(new LrcRedProvider(this))
    , lrcmuxProvider_(new LrcmuxProvider(this))
    , unisonProvider_(new UnisonProvider(this))
    , lyricsFallback_(new LyricsOvhProvider(this))
    , cache_(new CacheSqlite(cachePath))
    , musicDir_(musicDir)
{
    // Forward cover signals — stash artist/album is done in requestCover
    connect(coverProvider_, &CoverProvider::coverReady, this,
            [this](const QByteArray &data) {
                if (!pendingArtist_.isEmpty()) {
                    onCoverData(pendingArtist_, pendingAlbum_, data);
                    pendingArtist_.clear();
                    pendingAlbum_.clear();
                    refreshingCover_ = false;
                }
            });

    connect(coverProvider_, &CoverProvider::notFound, this,
            [this]() {
                if (!pendingArtist_.isEmpty()) {
                    // During a refresh, keep old cache/display on failure.
                    if (!refreshingCover_)
                        emit coverNotFound(pendingArtist_, pendingAlbum_);
                    pendingArtist_.clear();
                    pendingAlbum_.clear();
                    refreshingCover_ = false;
                }
            });

    // Forward lyrics signals
    connect(lyricsProvider_, &LrcLibProvider::lyricsFound, this,
            [this](const QString &plain, const QString &synced) {
                // Prefer plain text over synced — synced may be raw TTML/XML
                // (lrc.red, Unison) that is unreadable in a plain-text viewer.
                QString text = plain.isEmpty() ? synced : plain;
                if (!pendingArtistLyrics_.isEmpty()) {
                    // Cache lyrics
                    cachePut(QString("lyrics:%1:%2")
                             .arg(pendingArtistLyrics_, pendingTitleLyrics_),
                             text.toUtf8());
                    emit lyricsReady(pendingArtistLyrics_, pendingTitleLyrics_, text);
                    pendingArtistLyrics_.clear();
                    pendingTitleLyrics_.clear();
                    refreshingLyrics_ = false;
                }
            });

    // When LrcLib fails, try lrc.red
    connect(lyricsProvider_, &LrcLibProvider::notFound, this,
            [this]() {
                if (!pendingArtistLyrics_.isEmpty()) {
                    lrcRedProvider_->fetchLyrics(pendingArtistLyrics_,
                                                  pendingTitleLyrics_);
                }
            });

    connect(lyricsProvider_, &LrcLibProvider::error, this,
            [this](const QString &msg) {
                // Fall through to next provider on error too
                if (!pendingArtistLyrics_.isEmpty()) {
                    lrcRedProvider_->fetchLyrics(pendingArtistLyrics_,
                                                  pendingTitleLyrics_);
                }
            });

    // ── lrc.red ────────────────────────────────────────────────────
    connect(lrcRedProvider_, &LrcRedProvider::lyricsFound, this,
            [this](const QString &plain, const QString &synced) {
                QString text = plain.isEmpty() ? synced : plain;
                if (!pendingArtistLyrics_.isEmpty()) {
                    cachePut(QString("lyrics:%1:%2")
                             .arg(pendingArtistLyrics_, pendingTitleLyrics_),
                             text.toUtf8());
                    emit lyricsReady(pendingArtistLyrics_, pendingTitleLyrics_, text);
                    pendingArtistLyrics_.clear();
                    pendingTitleLyrics_.clear();
                    refreshingLyrics_ = false;
                }
            });

    connect(lrcRedProvider_, &LrcRedProvider::notFound, this,
            [this]() {
                if (!pendingArtistLyrics_.isEmpty()) {
                    lrcmuxProvider_->fetchLyrics(pendingArtistLyrics_,
                                                  pendingTitleLyrics_);
                }
            });

    connect(lrcRedProvider_, &LrcRedProvider::error, this,
            [this](const QString &msg) {
                emit error(msg);
                if (!pendingArtistLyrics_.isEmpty()) {
                    lrcmuxProvider_->fetchLyrics(pendingArtistLyrics_,
                                                  pendingTitleLyrics_);
                }
            });

    // ── Lrcmux ─────────────────────────────────────────────────────
    connect(lrcmuxProvider_, &LrcmuxProvider::lyricsFound, this,
            [this](const QString &plain, const QString &synced) {
                QString text = plain.isEmpty() ? synced : plain;
                if (!pendingArtistLyrics_.isEmpty()) {
                    cachePut(QString("lyrics:%1:%2")
                             .arg(pendingArtistLyrics_, pendingTitleLyrics_),
                             text.toUtf8());
                    emit lyricsReady(pendingArtistLyrics_, pendingTitleLyrics_, text);
                    pendingArtistLyrics_.clear();
                    pendingTitleLyrics_.clear();
                    refreshingLyrics_ = false;
                }
            });

    connect(lrcmuxProvider_, &LrcmuxProvider::notFound, this,
            [this]() {
                if (!pendingArtistLyrics_.isEmpty()) {
                    unisonProvider_->fetchLyrics(pendingArtistLyrics_,
                                                  pendingTitleLyrics_);
                }
            });

    connect(lrcmuxProvider_, &LrcmuxProvider::error, this,
            [this](const QString &msg) {
                emit error(msg);
                if (!pendingArtistLyrics_.isEmpty()) {
                    unisonProvider_->fetchLyrics(pendingArtistLyrics_,
                                                  pendingTitleLyrics_);
                }
            });

    // ── Unison ─────────────────────────────────────────────────────
    connect(unisonProvider_, &UnisonProvider::lyricsFound, this,
            [this](const QString &plain, const QString &synced) {
                QString text = plain.isEmpty() ? synced : plain;
                if (!pendingArtistLyrics_.isEmpty()) {
                    cachePut(QString("lyrics:%1:%2")
                             .arg(pendingArtistLyrics_, pendingTitleLyrics_),
                             text.toUtf8());
                    emit lyricsReady(pendingArtistLyrics_, pendingTitleLyrics_, text);
                    pendingArtistLyrics_.clear();
                    pendingTitleLyrics_.clear();
                    refreshingLyrics_ = false;
                }
            });

    connect(unisonProvider_, &UnisonProvider::notFound, this,
            [this]() {
                if (!pendingArtistLyrics_.isEmpty()) {
                    lyricsFallback_->fetchLyrics(pendingArtistLyrics_,
                                                  pendingTitleLyrics_);
                }
            });

    connect(unisonProvider_, &UnisonProvider::error, this,
            [this](const QString &msg) {
                emit error(msg);
                if (!pendingArtistLyrics_.isEmpty()) {
                    lyricsFallback_->fetchLyrics(pendingArtistLyrics_,
                                                  pendingTitleLyrics_);
                }
            });

    // Forward Unison attribution info.
    connect(unisonProvider_, &UnisonProvider::attributionInfo, this,
            &MetadataManager::lyricsAttribution);

    // ── Lyrics.ovh fallback ─────────────────────────────────────────
    connect(lyricsFallback_, &LyricsOvhProvider::lyricsFound, this,
            [this](const QString &plain) {
                if (!pendingArtistLyrics_.isEmpty()) {
                    cachePut(QString("lyrics:%1:%2")
                             .arg(pendingArtistLyrics_, pendingTitleLyrics_),
                             plain.toUtf8());
                    emit lyricsReady(pendingArtistLyrics_, pendingTitleLyrics_, plain);
                    pendingArtistLyrics_.clear();
                    pendingTitleLyrics_.clear();
                    refreshingLyrics_ = false;
                }
            });

    connect(lyricsFallback_, &LyricsOvhProvider::notFound, this,
            [this]() {
                if (!pendingArtistLyrics_.isEmpty()) {
                    // During a refresh, keep old cache/display on failure.
                    if (!refreshingLyrics_)
                        emit lyricsNotFound(pendingArtistLyrics_, pendingTitleLyrics_);
                    pendingArtistLyrics_.clear();
                    pendingTitleLyrics_.clear();
                    refreshingLyrics_ = false;
                }
            });

    connect(lyricsFallback_, &LyricsOvhProvider::error, this,
            [this](const QString &msg) {
                emit error(msg);
            });
}

MetadataManager::~MetadataManager()
{
    delete cache_;
}

void MetadataManager::requestCover(const QString &artist, const QString &album,
                                   const QString &uri)
{
    const auto &cfg = Config::instance();
    if (!cfg.metadataEnabled())
        return;

    // Check cache first
    QString key = QString("cover:%1:%2").arg(artist, album);
    QByteArray cached = cache_->get(key);
    if (!cached.isEmpty()) {
        emit coverReady(artist, album, cached);
        return;
    }

    // Тот же artist/album уже в полёте — не обрываем и не перезапускаем.
    // Иначе два потребителя (coverWidget_ и nowPlaying_->cover_),
    // вызывающие requestCover для одного трэка, будут каскадно отменять
    // друг друга через AsyncDownload::cancel(), удваивая время загрузки.
    if (pendingArtist_ == artist && pendingAlbum_ == album)
        return;

    // Stash for signal forwarding
    pendingArtist_ = artist;
    pendingAlbum_  = album;

    // Forward to CoverProvider (which handles local files first, then cascade)
    coverProvider_->fetchCover(artist, album, uri);
}

void MetadataManager::requestLyrics(const QString &artist, const QString &title,
                                     const QString &uri)
{
    const auto &cfg = Config::instance();
    if (!cfg.metadataEnabled()) {
        emit lyricsNotFound(artist, title);
        return;
    }

    QString key = QString("lyrics:%1:%2").arg(artist, title);
    QByteArray cached = cache_->get(key);
    if (!cached.isEmpty()) {
        // Backward compat: old cache may contain raw TTML/XML from synced
        // lyrics.  Strip tags so the user doesn't see <tt><span>… in UI.
        QString text = QString::fromUtf8(cached);
        if (looksLikeXml(text))
            text = stripXmlTags(text);
        emit lyricsReady(artist, title, text);
        return;
    }

    // Try embedded lyrics from the audio file's own tags before hitting
    // the network — many files carry USLT or LYRICS tag.
    if (cfg.metadataLocal() && !uri.isEmpty() && !musicDir_.isEmpty()) {
        const QString absPath = musicDir_ + QLatin1Char('/') + uri;
        const QString embedded = readEmbeddedLyrics(absPath);
        if (!embedded.isEmpty()) {
            cachePut(key, embedded.toUtf8());
            emit lyricsReady(artist, title, embedded);
            return;
        }
    }

    if (!cfg.metadataOnline()) {
        emit lyricsNotFound(artist, title);
        return;
    }

    // Stash for later forwarding
    pendingArtistLyrics_ = artist;
    pendingTitleLyrics_ = title;

    lyricsProvider_->fetchLyrics(artist, title);
}

void MetadataManager::setMusicDir(const QString &dir)
{
    coverProvider_->setMusicDir(dir);
}

void MetadataManager::refreshCover(const QString &artist, const QString &album,
                                   const QString &uri)
{
    if (!Config::instance().metadataEnabled())
        return;

    // Mark as refresh so notFound doesn't destroy existing cache/display.
    refreshingCover_ = true;

    // Bypass pending dedup and call the full provider pipeline directly.
    pendingArtist_ = artist;
    pendingAlbum_  = album;

    // Skip CoverProvider's own cache as well via skipCache flag.
    coverProvider_->fetchCover(artist, album, uri, /*skipCache=*/true);
}

void MetadataManager::refreshLyrics(const QString &artist, const QString &title,
                                    const QString &uri)
{
    if (!Config::instance().metadataEnabled())
        return;

    refreshingLyrics_ = true;

    // Bypass requestLyrics' cache check: stash pending and call the
    // first provider directly.  The fallback chain still works on notFound.
    pendingArtistLyrics_ = artist;
    pendingTitleLyrics_ = title;

    // Try embedded first (fast, local).
    bool triedEmbedded = false;
    if (!uri.isEmpty() && !musicDir_.isEmpty()) {
        const QString absPath = musicDir_ + QLatin1Char('/') + uri;
        const QString embedded = readEmbeddedLyrics(absPath);
        if (!embedded.isEmpty()) {
            // New data: overwrite cache and emit.
            cachePut(QString("lyrics:%1:%2").arg(artist, title),
                     embedded.toUtf8());
            emit lyricsReady(artist, title, embedded);
            return;
        }
        triedEmbedded = true;
    }

    if (!Config::instance().metadataOnline()) {
        // Nothing online and embedded failed — keep old cache/display.
        pendingArtistLyrics_.clear();
        pendingTitleLyrics_.clear();
        refreshingLyrics_ = false;
        return;
    }

    lyricsProvider_->fetchLyrics(artist, title);
}

void MetadataManager::onCoverData(const QString &artist, const QString &album,
                                  const QByteArray &data)
{
    cache_->put(QString("cover:%1:%2").arg(artist, album), data);
    emit coverReady(artist, album, data);
}

void MetadataManager::onCoverNotFound(const QString &artist, const QString &album)
{
    emit coverNotFound(artist, album);
}

// ── Helper (private) ────────────────────────────────────────────────────────

void MetadataManager::cachePut(const QString &key, const QByteArray &data)
{
    cache_->put(key, data);
}
