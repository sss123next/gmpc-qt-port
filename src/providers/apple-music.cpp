/**
 * @file apple-music.cpp
 * @brief Apple Music / iTunes Search API cover-art provider implementation.
 *
 * Uses the public iTunes Search API (no key required) to find album artwork.
 * Searches by artist and album name, then downloads the cover image at
 * the requested resolution via URL size substitution.
 */

#include "apple-music.h"
#include "tools/async-download.h"

#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QDebug>

// ── Constants ──────────────────────────────────────────────────────────────

/// iTunes Search API endpoint (free, no key).
static const char *kItunesSearch = "https://itunes.apple.com/search";

// ── Constructor / Destructor ───────────────────────────────────────────────

AppleMusicProvider::AppleMusicProvider(QObject *parent)
    : QObject(parent), dl_(new AsyncDownload(this))
{}

AppleMusicProvider::~AppleMusicProvider()
{
    if (dl_)
        dl_->cancel();
    if (!ownsDl_)
        dl_ = nullptr;
}

void AppleMusicProvider::setDownloader(AsyncDownload *dl)
{
    if (ownsDl_ && dl_)
        delete dl_;
    dl_ = dl;
    ownsDl_ = false;
}

// ── Public API ─────────────────────────────────────────────────────────────

void AppleMusicProvider::fetchCover(const QString &artist, const QString &album,
                                    int targetSizePx)
{
    searchedArtist_ = artist;
    searchedAlbum_  = album;
    targetSize_ = qBound(100, targetSizePx, 3000);
    if (targetSize_ < 100) targetSize_ = 600;

    QUrl url(kItunesSearch);
    QUrlQuery q;
    q.addQueryItem("term", QString("%1 %2").arg(artist, album));
    q.addQueryItem("entity", "album");
    q.addQueryItem("limit", "5");
    q.addQueryItem("country", "us");
    url.setQuery(q);

    dl_->get(url, [this](bool ok, const QByteArray &data) {
        if (!ok || data.isEmpty()) {
            emit error(tr("Apple Music: network error"));
            return;
        }
        onSearchReply(true, data);
    });
}

// ── Response Handlers ─────────────────────────────────────────────────────

void AppleMusicProvider::onSearchReply(bool ok, const QByteArray &data)
{
    if (!ok) { emit error(tr("Apple Music: network error")); return; }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) { emit error(tr("Apple Music: invalid JSON")); return; }

    const QJsonObject root = doc.object();
    const QJsonArray results = root.value("results").toArray();
    if (results.isEmpty()) { emit notFound(); return; }

    // Find the best matching album result.
    // Match by artist name (case-insensitive) as required by the plan.
    QString artworkUrl;
    const QString qArtist = searchedArtist_.trimmed().toLower();
    const QString qAlbum  = searchedAlbum_.trimmed().toLower();

    for (const auto &item : results) {
        const QJsonObject obj = item.toObject();
        const QString wrapperType = obj.value("wrapperType").toString();
        const QString collectionType = obj.value("collectionType").toString();
        if (wrapperType != "collection" || collectionType != "Album")
            continue;

        // Prefer results whose artist name matches what we searched for.
        const QString resultArtist = obj.value("artistName").toString().trimmed().toLower();
        if (!resultArtist.contains(qArtist) && !qArtist.contains(resultArtist))
            continue;  // artist mismatch — skip

        // Prefer artworkUrl100 over artworkUrl60
        const QString url100 = obj.value("artworkUrl100").toString();
        if (!url100.isEmpty()) {
            artworkUrl = url100;
            break;
        }
        const QString url60 = obj.value("artworkUrl60").toString();
        if (!url60.isEmpty()) {
            artworkUrl = url60;
            // continue searching for a 100x100
        }
    }

    // If no artist-matched result found, take the first Album result as fallback.
    if (artworkUrl.isEmpty()) {
        for (const auto &item : results) {
            const QJsonObject obj = item.toObject();
            const QString wrapperType = obj.value("wrapperType").toString();
            const QString collectionType = obj.value("collectionType").toString();
            if (wrapperType != "collection" || collectionType != "Album")
                continue;
            const QString url100 = obj.value("artworkUrl100").toString();
            if (!url100.isEmpty()) { artworkUrl = url100; break; }
            const QString url60 = obj.value("artworkUrl60").toString();
            if (!url60.isEmpty()) { artworkUrl = url60; }
        }
    }

    if (artworkUrl.isEmpty()) { emit notFound(); return; }

    // Resize to target
    const QString resized = resizeArtworkUrl(artworkUrl, targetSize_);

    dl_->get(QUrl(resized), [this](bool ok2, const QByteArray &data2) {
        onImageReply(ok2, data2);
    });
}

void AppleMusicProvider::onImageReply(bool ok, const QByteArray &data)
{
    if (!ok || data.isEmpty()) {
        emit error(tr("Apple Music: image download failed"));
        return;
    }
    emit coverFound(data);
}

// ── URL size substitution ─────────────────────────────────────────────────

QString AppleMusicProvider::resizeArtworkUrl(const QString &url, int px)
{
    // URL format: /100x100bb.jpg  or  /60x60bb.jpg
    // Replace the last occurrence of /NNNxNNNbb.jpg with /{px}x{px}bb.jpg
    QRegularExpression re(R"(/\d+x\d+bb\.jpg$)");
    QRegularExpressionMatch m = re.match(url);
    if (m.hasMatch()) {
        QString replacement = QStringLiteral("/%1x%1bb.jpg").arg(px);
        return QString(url).replace(m.capturedStart(), m.capturedLength(), replacement);
    }
    // Also try .png variant
    QRegularExpression rePng(R"(/\d+x\d+bb\.png$)");
    QRegularExpressionMatch mPng = rePng.match(url);
    if (mPng.hasMatch()) {
        QString replacement = QStringLiteral("/%1x%1bb.png").arg(px);
        return QString(url).replace(mPng.capturedStart(), mPng.capturedLength(), replacement);
    }
    return url;
}
