#pragma once

/**
 * @file discogs.h
 * @brief Discogs API provider for cover art (search + download).
 *
 * Two modes:
 *   - With token:   60 req/min (authenticated)
 *   - Without token: 25 req/min — enough for occasional lookups
 *
 * API:  GET https://api.discogs.com/database/search?q=ARTIST+ALBUM&token=TOKEN
 *       → results[].cover_image (URL of the cover image)
 *
 * Rate limits: https://www.discogs.com/developers#page:home,header:home-rate-limiting
 */

#include <QObject>
#include <QString>
#include <QByteArray>
#include "tools/async-download.h"

/// Cover-art provider via Discogs search API.
class DiscogsProvider : public QObject {
    Q_OBJECT
public:
    /// @param token  Discogs personal access token (empty = unauthenticated).
    explicit DiscogsProvider(const QString &token, QObject *parent = nullptr);
    ~DiscogsProvider() override;

    void setDownloader(AsyncDownload *dl);

    /// Fetch cover image for artist/album.
    void fetchCover(const QString &artist, const QString &album);

signals:
    void coverFound(const QByteArray &imageData);
    void notFound();
    void error(const QString &message);

public:   // ── Test seams (exposed for fuzz testing, no network) ──────────
    void testInjectSearchReply(bool ok, const QByteArray &data)
        { onSearchReply(ok, data); }
    void testInjectImageReply(bool ok, const QByteArray &data)
        { onImageReply(ok, data); }

private:
    void onSearchReply(bool ok, const QByteArray &data);
    void onImageReply(bool ok, const QByteArray &data);

    AsyncDownload *dl_;
    QString token_;
    bool ownsDl_ = true;
};
