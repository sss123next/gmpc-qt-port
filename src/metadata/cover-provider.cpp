#include "cover-provider.h"
#include "tools/async-download.h"

#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QDebug>

// ══════════════════════════════════════════════════════════════════════════════
// CoverProvider — cascading MusicBrainz → Last.FM → Deezer
// ══════════════════════════════════════════════════════════════════════════════

CoverProvider::CoverProvider(const QString &lastFmApiKey,
                             const QString &cachePath,
                             QObject *parent)
    : QObject(parent)
    , lastFmApiKey_(lastFmApiKey)
    , cache_(cachePath)
    , dl_(new AsyncDownload(this))
    , ownsDl_(true)
{}

CoverProvider::~CoverProvider()
{
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

void CoverProvider::fetchCover(const QString &artist, const QString &album)
{
    artist_ = artist;
    album_  = album;

    // Step 1 — check cache
    QByteArray cached = cache_.get(cacheKey());
    if (!cached.isEmpty()) {
        emit coverReady(cached);
        return;
    }

    // Step 2 — MusicBrainz (primary, key-free)
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

// ── MusicBrainz ─────────────────────────────────────────────────────────────

static const char *kMBSearchUrl = "https://musicbrainz.org/ws/2/release/";
static const char *kCAAUrl      = "https://coverartarchive.org/release/";
static const char *kUserAgent   = "GMPC-Qt/0.1";

void CoverProvider::tryMusicBrainz()
{
    QUrl url(kMBSearchUrl);
    QUrlQuery q;
    q.addQueryItem("query", QString("artist:\"%1\" AND release:\"%2\"")
                                .arg(artist_, album_));
    q.addQueryItem("fmt", "json");
    q.addQueryItem("limit", "1");
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", kUserAgent);
    req.setRawHeader("Accept", "application/json");

    dl_->get(req, [this](bool ok, const QByteArray &data) {
        if (!ok) { onMBError(tr("MusicBrainz: network error")); return; }

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) { onMBError(tr("MusicBrainz: invalid JSON")); return; }

        QJsonArray releases = doc.object().value("releases").toArray();
        if (releases.isEmpty()) { onMBNotFound(); return; }

        QString mbid = releases[0].toObject().value("id").toString();
        if (mbid.isEmpty()) { onMBNotFound(); return; }

        // Fetch from Cover Art Archive
        QUrl caaUrl(QString("%1%2/front").arg(kCAAUrl, mbid));
        QNetworkRequest caaReq(caaUrl);
        caaReq.setRawHeader("User-Agent", kUserAgent);

        dl_->get(caaReq, [this](bool ok2, const QByteArray &data2) {
            if (!ok2) { onMBNotFound(); return; }
            if (data2.isEmpty()) { onMBNotFound(); return; }
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
