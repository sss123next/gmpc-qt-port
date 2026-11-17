#include "cover-provider.h"
#include "providers/discogs.h"
#include "tools/async-download.h"

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
    , ownsDl_(true)
    , musicDir_(musicDir)
{
    // Forward Discogs signals
    connect(discogs_, &DiscogsProvider::coverFound,
            this, &CoverProvider::onDiscogsCover);
    connect(discogs_, &DiscogsProvider::error,
            this, &CoverProvider::onDiscogsError);
    connect(discogs_, &DiscogsProvider::notFound,
            this, [this]() { onDiscogsError(QString()); });
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
                               const QString &uri)
{
    artist_ = artist;
    album_  = album;

    qDebug() << "CoverProvider::fetchCover" << artist << album << "uri:" << uri;

    // Step 1 — check cache
    QByteArray cached = cache_.get(cacheKey());
    if (!cached.isEmpty()) {
        qDebug() << "  → cache hit";
        emit coverReady(cached);
        return;
    }

    // Step 2 — local file alongside the song
    if (!uri.isEmpty() && !musicDir_.isEmpty()) {
        if (tryLocalFile(uri)) {
            // tryLocalFile emits coverReady on success
            return;
        }
    }

    qDebug() << "  → cache + local miss, trying MusicBrainz";
    // Step 3 — MusicBrainz (primary, key-free)
    tryMusicBrainz();
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

    dl_->get(req, [this](bool ok, const QByteArray &data) {
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

        dl_->get(caaReq, [this](bool ok2, const QByteArray &data2) {
            if (!ok2) { qDebug() << "  CAA: network error"; onMBNotFound(); return; }
            if (data2.isEmpty()) { qDebug() << "  CAA: empty response"; onMBNotFound(); return; }
            qDebug() << "  CAA: got" << data2.size() << "bytes";
            tryComplete(data2, "MusicBrainz/CAA");
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
    // Fall through to Last.FM
    if (!lastFmApiKey_.isEmpty())
        tryLastFm();
    else
        tryDeezer();
}

void CoverProvider::onMBError(const QString &msg)
{
    Q_UNUSED(msg);
    onMBNotFound();
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

    dl_->get(url, [this](bool ok, const QByteArray &data) {
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
            onLFUrl(bestUrl);
        else
            onLFError(tr("Last.FM: no cover image found"));
    });
}

void CoverProvider::onLFUrl(const QString &url)
{
    // Download the actual image
    dl_->get(QUrl(url), [this](bool ok, const QByteArray &data) {
        if (!ok || data.isEmpty()) {
            tryDeezer();
            return;
        }
        tryComplete(data, "Last.FM");
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

    dl_->get(url, [this](bool ok, const QByteArray &data) {
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
        dl_->get(QUrl(coverBig), [this](bool ok2, const QByteArray &data2) {
            if (!ok2 || data2.isEmpty()) {
                onDeezerError(tr("Deezer: download failed"));
                return;
            }
            tryComplete(data2, "Deezer");
        });
    });
}

void CoverProvider::onDeezerCover(const QByteArray &data)
{
    tryComplete(data, "Deezer");
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
    discogs_->fetchCover(artist_, album_);
}

void CoverProvider::onDiscogsCover(const QByteArray &data)
{
    tryComplete(data, "Discogs");
}

void CoverProvider::onDiscogsError(const QString &msg)
{
    Q_UNUSED(msg);
    emit notFound();
}

// ── Completion ──────────────────────────────────────────────────────────────

void CoverProvider::tryComplete(const QByteArray &data, const QString &label)
{
    Q_UNUSED(label);
    cache_.put(cacheKey(), data);
    emit coverReady(data);
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
