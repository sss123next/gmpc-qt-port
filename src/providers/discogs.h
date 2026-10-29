#pragma once

/**
 * @file discogs.h
 * @brief Discogs API provider (cover art).
 *
 * Requires a personal access token (free).
 * Rate limits: 60 req/min (authenticated), 25 req/min (unauthenticated).
 * Unauthenticated requests use a low limit — real usage requires a token.
 *
 * @code
 *   "https://api.discogs.com/database/search?q=ARTIST+ALBUM&token=TOKEN"
 * @endcode
 *
 * TODO — full implementation with OAuth / token support.
 */

#include <QObject>

/// Cover-art provider via Discogs API.
class DiscogsProvider : public QObject {
    Q_OBJECT
public:
    explicit DiscogsProvider(QObject *parent = nullptr);
    // TODO: fetchCover(), results parsing, token management
};

