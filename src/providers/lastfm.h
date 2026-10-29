#ifndef GMPCQT_LASTFM_H
#define GMPCQT_LASTFM_H

/**
 * @file lastfm.h
 * @brief Last.FM API provider (cover art + artist info).
 *
 * Uses the Last.FM REST API (album.getInfo) to fetch cover art.
 * Requires a free API key: https://www.last.fm/api
 *
 * Rate limit: ~5 req/s (soft).
 *
 * @code
 *   "https://ws.audioscrobbler.com/2.0/?method=album.getInfo"
 *   "&api_key=KEY&artist=ARTIST&album=ALBUM&format=json"
 * @endcode
 */

#include <QObject>
#include <QString>
#include <QByteArray>
#include "tools/async-download.h"

/// Cover-art provider via Last.FM API.
class LastFmProvider : public QObject {
    Q_OBJECT
public:
    explicit LastFmProvider(const QString &apiKey, QObject *parent = nullptr);

    /// Fetch cover URL for artist/album.  Emits coverFound() on success.
    void fetchCover(const QString &artist, const QString &album);

signals:
    /// Emitted when the cover image URL is resolved.
    void coverFound(const QString &imageUrl);
    /// Emitted on network or API error.
    void error(const QString &message);

private:
    void onReply(bool ok, const QByteArray &data);

    QString apiKey_;
    AsyncDownload *dl_;
};

#endif // GMPCQT_LASTFM_H
