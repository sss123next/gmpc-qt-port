#include "cover-provider.h"
#include "providers/discogs.h"
#include "providers/apple-music.h"
#include "tools/async-download.h"
#include "tag-reader.h"
#include "core/config.h"

#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QDebug>

// ══════════════════════════════════════════════════════════════════════════════
// CoverProvider — cascading MusicBrainz → Last.FM → Deezer → Discogs
// ══════════════════════════════════════════════════════════════════════════════

CoverProvider::CoverProvider(const QString &lastFmApiKey,
                             const QString &discogsToken,
                             const QString &cachePath,
                             const QString &musicDir,
                             QObject *parent)
    : QObject(parent)
    , lastFmApiKey_(lastFmApiKey)
    , discogsToken_(discogsToken)
    , cache_(cachePath)
    , dl_(new AsyncDownload(this))
    , discogs_(new DiscogsProvider(discogsToken_, this))
    , appleMusic_(new AppleMusicProvider(this))
    , ownsDl_(true)
    , musicDir_(musicDir)
{
    // Forward Apple Music signals — connected once, dispatch through seq_-aware methods.
    connect(appleMusic_, &AppleMusicProvider::coverFound, this, [this](const QByteArray &data) {
        onAMCover(data);
    });
    connect(appleMusic_, &AppleMusicProvider::notFound, this, [this]() {
        onAMNotFound();
    });
    connect(appleMusic_, &AppleMusicProvider::error, this, [this](const QString &msg) {
        Q_UNUSED(msg);
        onAMError(msg);
    });

    // Forward Discogs signals — re-connected per-request in tryDiscogs()
    // with the correct request sequence captured.
    Q_UNUSED(discogs_);
}

CoverProvider::~CoverProvider()
{
    if (dl_)
        dl_->cancel();
    if (!ownsDl_) dl_ = nullptr;
}

void CoverProvider::setDownloader(AsyncDownload *dl)
{
    if (ownsDl_ && dl_) {
        delete dl_;
    }
    dl_ = dl;
    ownsDl_ = false;
}

QString CoverProvider::cacheKey() const
{
    return QString("cover:%1:%2").arg(artist_, album_);
}

void CoverProvider::fetchCover(const QString &artist, const QString &album,
                               const QString &uri, bool skipCache)
{
    const auto &cfg = Config::instance();

    // Bump request serial — every async callback must check its captured
    // seq against this before processing, so stale cross-track responses
    // are silently dropped.
    ++seq_;

    artist_ = artist;
    album_  = album;

    // Cancel any in-flight Discogs request from a previous cascade.
    discogs_->cancel();

    qDebug() << "CoverProvider::fetchCover" << artist << album << "uri:" << uri;

    // Master switch: completely disable all metadata.
    if (!cfg.metadataEnabled()) {
        qDebug() << "  → metadata disabled";
        emit notFound();
        return;
    }

    // Step 1 — check cache (skip if refresh requested)
    if (!skipCache) {
        QByteArray cached = cache_.get(cacheKey());
        if (!cached.isEmpty()) {
            qDebug() << "  → cache hit";
            emit coverReady(cached);
            return;
        }
    }

    // Step 2 — local file alongside the song (cover.jpg, folder.jpg, etc.)
    if (cfg.metadataLocal() && !uri.isEmpty() && !musicDir_.isEmpty()) {
        if (tryLocalFile(uri)) {
            // tryLocalFile emits coverReady on success
            return;
        }
    }

    // Step 3 — embedded cover art in the file's own tags
    if (cfg.metadataLocal() && !uri.isEmpty() && !musicDir_.isEmpty()) {
        const QString absPath = musicDir_ + QLatin1Char('/') + uri;
        EmbeddedCover emb = readEmbeddedCover(absPath);
        if (!emb.data.isEmpty()) {
            qDebug() << "  → embedded cover found in tags";
            cache_.put(cacheKey(), emb.data);
            emit coverReady(emb.data);
            return;
        }
    }

    qDebug() << "  → cache + local miss, trying online";

    // Step 4 — online providers
    if (cfg.metadataOnline()) {
        tryMusicBrainz();
    } else {
        emit notFound();
    }
}

void CoverProvider::tryCache()
{
    QByteArray cached = cache_.get(cacheKey());
    if (!cached.isEmpty()) {
        emit coverReady(cached);
    } else {
        emit notFound();
    }
}

// ── Local file lookup ───────────────────────────────────────────────────────

static const char *kLocalCoverNames[] = {
    "cover.jpg", "cover.png",
    "folder.jpg", "folder.png",
    "album.jpg", "album.png",
    "front.jpg", "front.png",
    nullptr   // sentinel
};

bool CoverProvider::tryLocalFile(const QString &uri)
{
    // URI — относительный путь внутри music_directory, как его отдал MPD.
    // Например: "local/Penumbra/2002 - The Last Bewitchment/08_A_Torrent_of_Fears.opus"
    // Ничего не обрезаем: это честный путь.
    const QFileInfo fi(uri);
    const QString songDir = fi.path();  // e.g. "local/Penumbra/2002 - The Last Bewitchment"

    // Build absolute directory path
    const QDir dir(musicDir_ + QLatin1Char('/') + songDir);
    if (!dir.exists()) {
        qDebug() << "  tryLocalFile: dir not found:" << dir.absolutePath();
        return false;
    }

    qDebug() << "  tryLocalFile: searching in" << dir.absolutePath();
    for (const char **name = kLocalCoverNames; *name != nullptr; ++name) {
        const QString path = dir.absoluteFilePath(QLatin1StringView(*name));
        QFile file(path);
        if (!file.exists())
            continue;

        qDebug() << "  local cover found:" << path;
        if (!file.open(QIODevice::ReadOnly))
            continue;

        const QByteArray data = file.readAll();
        file.close();

        if (data.isEmpty())
            continue;

        qDebug() << "  local cover loaded:" << data.size() << "bytes";
        // Cache and emit
        cache_.put(cacheKey(), data);
        emit coverReady(data);
        return true;
    }

    qDebug() << "  tryLocalFile: no cover file found in" << dir.absolutePath();
    return false;
}

// ── MusicBrainz ─────────────────────────────────────────────────────────────

static const char *kMBSearchUrl = "https://musicbrainz.org/ws/2/release/";
static const char *kCAAUrl      = "https://coverartarchive.org/release/";
static const char *kUserAgent   = "GMPC-Qt/0.1 ( Qt/" QT_VERSION_STR " )";

void CoverProvider::tryMusicBrainz()
{
    QUrl url(kMBSearchUrl);
    QUrlQuery q;
    q.addQueryItem("query", QString("artist:\"%1\" AND release:\"%2\"")
                                .arg(artist_, album_));
    q.addQueryItem("fmt", "json");
    q.addQueryItem("limit", "1");
    url.setQuery(q);

    qDebug() << "  MusicBrainz: GET" << url.toString();

    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", kUserAgent);
    req.setRawHeader("Accept", "application/json");

    uint64_t expected = seq_;
    const QString reqArtist = artist_;
    const QString reqAlbum  = album_;
    dl_->get(req, [this, expected, reqArtist, reqAlbum](bool ok, const QByteArray &data) {
        if (isStale(expected)) {
            qDebug() << "  MusicBrainz: stale (seq" << expected << "!= " << seq_ << ")";
            return;
        }
        if (!ok) { qDebug() << "  MusicBrainz: network error"; onMBError(tr("MusicBrainz: network error")); return; }

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) { qDebug() << "  MusicBrainz: invalid JSON"; onMBError(tr("MusicBrainz: invalid JSON")); return; }

        QJsonArray releases = doc.object().value("releases").toArray();
        if (releases.isEmpty()) { qDebug() << "  MusicBrainz: no releases found"; onMBNotFound(); return; }

        QString mbid = releases[0].toObject().value("id").toString();
        if (mbid.isEmpty()) { qDebug() << "  MusicBrainz: empty MBID"; onMBNotFound(); return; }

        qDebug() << "  MusicBrainz: found MBID" << mbid;

        // Fetch from Cover Art Archive
        QUrl caaUrl(QString("%1%2/front").arg(kCAAUrl, mbid));
        QNetworkRequest caaReq(caaUrl);
        caaReq.setRawHeader("User-Agent", kUserAgent);

        dl_->get(caaReq, [this, expected, reqArtist, reqAlbum](bool ok2, const QByteArray &data2) {
            if (isStale(expected)) {
                qDebug() << "  CAA: stale (seq" << expected << "!= " << seq_ << ")";
                return;
            }
            if (!ok2) { qDebug() << "  CAA: network error"; onMBNotFound(); return; }
            if (data2.isEmpty()) { qDebug() << "  CAA: empty response"; onMBNotFound(); return; }
            qDebug() << "  CAA: got" << data2.size() << "bytes";
            tryComplete(reqArtist, reqAlbum, data2, "MusicBrainz/CAA");
        });
    });
}

void CoverProvider::onMBCover(const QByteArray &data)
{
    cache_.put(cacheKey(), data);
    emit coverReady(data);
}

void CoverProvider::onMBNotFound()
{
    // Fall through to Apple Music
    tryAppleMusic();
}

void CoverProvider::onMBError(const QString &msg)
{
    Q_UNUSED(msg);
    onMBNotFound();
}

// ── Apple Music / iTunes Search API ───────────────────────────────────────

static const char *kAMSearch = "https://itunes.apple.com/search";

void CoverProvider::tryAppleMusic()
{
    appleMusic_->fetchCover(artist_, album_, 600);
}

void CoverProvider::onAMCover(const QByteArray &data)
{
    if (data.isEmpty()) { onAMNotFound(); return; }
    cache_.put(cacheKey(), data);
    emit coverReady(data);
}

void CoverProvider::onAMNotFound()
{
    // Fall through to Last.FM (or Deezer if no API key)
    if (!lastFmApiKey_.isEmpty())
        tryLastFm();
    else
        tryDeezer();
}

void CoverProvider::onAMError(const QString &msg)
{
    Q_UNUSED(msg);
    onAMNotFound();
}

// ── Last.FM ─────────────────────────────────────────────────────────────────

static const char *kLFBase = "https://ws.audioscrobbler.com/2.0/";

void CoverProvider::tryLastFm()
{
    QUrl url(kLFBase);
    QUrlQuery q;
    q.addQueryItem("method", "album.getInfo");
    q.addQueryItem("api_key", lastFmApiKey_);
    q.addQueryItem("artist", artist_);
    q.addQueryItem("album", album_);
    q.addQueryItem("format", "json");
    url.setQuery(q);

    uint64_t expected = seq_;
    const QString reqArtist = artist_;
    const QString reqAlbum  = album_;
    dl_->get(url, [this, expected, reqArtist, reqAlbum](bool ok, const QByteArray &data) {
        if (isStale(expected)) {
            qDebug() << "  Last.FM: stale (seq" << expected << "!= " << seq_ << ")";
            return;
        }
        if (!ok) { onLFError(tr("Last.FM: network error")); return; }

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) { onLFError(tr("Last.FM: invalid JSON")); return; }

        QJsonObject root = doc.object();
        if (root.contains("error")) { onLFError(root.value("message").toString()); return; }

        QJsonObject album = root.value("album").toObject();
        QJsonArray images = album.value("image").toArray();
        QString bestUrl;
        for (const auto &img : images) {
            QString url = img.toObject().value("#text").toString();
            if (!url.isEmpty()) bestUrl = url;
        }

        if (!bestUrl.isEmpty())
            onLFUrl(bestUrl, expected, reqArtist, reqAlbum);
        else
            onLFError(tr("Last.FM: no cover image found"));
    });
}

void CoverProvider::onLFUrl(const QString &url, uint64_t expected,
                             const QString &reqArtist, const QString &reqAlbum)
{
    // Download the actual image
    dl_->get(QUrl(url), [this, expected, reqArtist, reqAlbum](bool ok, const QByteArray &data) {
        if (isStale(expected)) {
            qDebug() << "  Last.FM image: stale (seq" << expected << "!= " << seq_ << ")";
            return;
        }
        if (!ok || data.isEmpty()) {
            tryDeezer();
            return;
        }
        tryComplete(reqArtist, reqAlbum, data, "Last.FM");
    });
}

void CoverProvider::onLFError(const QString &msg)
{
    Q_UNUSED(msg);
    tryDeezer();
}

// ── Deezer ──────────────────────────────────────────────────────────────────

static const char *kDeezerSearch = "https://api.deezer.com/search/album";

void CoverProvider::tryDeezer()
{
    QUrl url(kDeezerSearch);
    QUrlQuery q;
    q.addQueryItem("q", QString("%1 %2").arg(artist_, album_));
    url.setQuery(q);

    uint64_t expected = seq_;
    const QString reqArtist = artist_;
    const QString reqAlbum  = album_;
    dl_->get(url, [this, expected, reqArtist, reqAlbum](bool ok, const QByteArray &data) {
        if (isStale(expected)) {
            qDebug() << "  Deezer search: stale (seq" << expected << "!= " << seq_ << ")";
            return;
        }
        if (!ok) { onDeezerError(tr("Deezer: network error")); return; }

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) { onDeezerError(tr("Deezer: invalid JSON")); return; }

        QJsonArray results = doc.object().value("data").toArray();
        if (results.isEmpty()) { onDeezerError(tr("Deezer: no results")); return; }

        // Try to match by artist name
        QString coverBig;
        for (const auto &item : results) {
            QJsonObject obj = item.toObject();
            QString deezerArtist = obj.value("artist").toObject().value("name").toString();
            // Case-insensitive match on artist
            if (deezerArtist.compare(artist_, Qt::CaseInsensitive) == 0) {
                coverBig = obj.value("cover_big").toString();
                if (!coverBig.isEmpty()) break;
            }
        }
        // Fallback: first result even if artist doesn't match
        if (coverBig.isEmpty()) {
            coverBig = results[0].toObject().value("cover_big").toString();
        }

        if (coverBig.isEmpty()) {
            onDeezerError(tr("Deezer: no cover URL"));
            return;
        }

        // Download the cover image
        dl_->get(QUrl(coverBig), [this, expected, reqArtist, reqAlbum](bool ok2, const QByteArray &data2) {
            if (isStale(expected)) {
                qDebug() << "  Deezer image: stale (seq" << expected << "!= " << seq_ << ")";
                return;
            }
            if (!ok2 || data2.isEmpty()) {
                onDeezerError(tr("Deezer: download failed"));
                return;
            }
            tryComplete(reqArtist, reqAlbum, data2, "Deezer");
        });
    });
}

void CoverProvider::onDeezerError(const QString &msg)
{
    Q_UNUSED(msg);
    tryDiscogs();
}

// ── Discogs ─────────────────────────────────────────────────────────────────

static const char *kDiscogsLog = "discogs";

void CoverProvider::tryDiscogs()
{
    // Cancel any previous Discogs request (safety net — fetchCover() already
    // called discogs_->cancel(), but tryDiscogs may be called again later
    // from a stale cascade fall-through).
    discogs_->cancel();

    // Capture the track identity at request time so we cache with the
    // correct key even if the UI has moved to another track.
    const QString reqArtist = artist_;
    const QString reqAlbum  = album_;

    // Tear down any connections from a previous tryDiscogs call.
    disconnect(discogs_, &DiscogsProvider::coverFound, this, nullptr);
    disconnect(discogs_, &DiscogsProvider::error, this, nullptr);
    disconnect(discogs_, &DiscogsProvider::notFound, this, nullptr);

    connect(discogs_, &DiscogsProvider::coverFound, this,
            [this, reqArtist, reqAlbum](const QByteArray &data) {
        // Always cache with the requesting track's identity.
        const QString key = QString("cover:%1:%2").arg(reqArtist, reqAlbum);
        cache_.put(key, data);
        // Only emit if this track is still the one being requested.
        if (reqArtist == artist_ && reqAlbum == album_) {
            emit coverReady(data);
        } else {
            qDebug() << "  Discogs cover: cached" << data.size()
                     << "bytes for" << reqArtist << reqAlbum
                     << "(current:" << artist_ << album_ << ") — deferred";
        }
    });

    connect(discogs_, &DiscogsProvider::error, this,
            [this, reqArtist, reqAlbum](const QString &msg) {
        Q_UNUSED(msg); Q_UNUSED(reqArtist); Q_UNUSED(reqAlbum);
        emit notFound();
    });

    connect(discogs_, &DiscogsProvider::notFound, this,
            [this, reqArtist, reqAlbum]() {
        Q_UNUSED(reqArtist); Q_UNUSED(reqAlbum);
        emit notFound();
    });

    discogs_->fetchCover(artist_, album_);
}

// ── Completion ──────────────────────────────────────────────────────────────

void CoverProvider::tryComplete(const QString &artist, const QString &album,
                                 const QByteArray &data, const QString &label)
{
    Q_UNUSED(label);
    // Always cache with the requesting track's identity, not current
    // artist_/album_ — ensures data is preserved even if the UI already
    // moved to another track.
    const QString key = QString("cover:%1:%2").arg(artist, album);
    cache_.put(key, data);

    // Only emit coverReady if this track is still the one being requested.
    // Otherwise the data is cached for later reuse, but the UI should not
    // flash a cover that belongs to a previous track.
    if (artist == artist_ && album == album_) {
        emit coverReady(data);
    } else {
        qDebug() << "  " << label << ": cached" << data.size()
                 << "bytes for" << artist << album
                 << "(current:" << artist_ << album_ << ") — deferred";
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// DeezerProvider — standalone for testability
// ══════════════════════════════════════════════════════════════════════════════

DeezerProvider::DeezerProvider(QObject *parent)
    : QObject(parent), dl_(new AsyncDownload(this))
{}

DeezerProvider::~DeezerProvider()
{
    if (dl_)
        dl_->cancel();
    if (!ownsDl_) dl_ = nullptr;
}

void DeezerProvider::setDownloader(AsyncDownload *dl)
{
    if (ownsDl_ && dl_) {
        delete dl_;
    }
    dl_ = dl;
    ownsDl_ = false;
}

void DeezerProvider::fetchCover(const QString &artist, const QString &album)
{
    QUrl url(kDeezerSearch);
    QUrlQuery q;
    q.addQueryItem("q", QString("%1 %2").arg(artist, album));
    url.setQuery(q);

    dl_->get(url, [this](bool ok, const QByteArray &data) {
        onSearchReply(ok, data);
    });
}

void DeezerProvider::onSearchReply(bool ok, const QByteArray &data)
{
    if (!ok) { emit error(tr("Deezer: network error")); return; }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) { emit error(tr("Deezer: invalid JSON")); return; }

    QJsonArray results = doc.object().value("data").toArray();
    if (results.isEmpty()) { emit notFound(); return; }

    // Take first album's cover_big
    QString coverBig = results[0].toObject().value("cover_big").toString();
    if (coverBig.isEmpty()) { emit notFound(); return; }

    dl_->get(QUrl(coverBig), [this](bool ok2, const QByteArray &data2) {
        onImageReply(ok2, data2);
    });
}

void DeezerProvider::onImageReply(bool ok, const QByteArray &data)
{
    if (!ok || data.isEmpty()) { emit error(tr("Deezer: image download failed")); return; }
    emit coverFound(data);
}
