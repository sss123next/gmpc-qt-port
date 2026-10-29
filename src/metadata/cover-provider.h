#pragma once

/**
 * @file cover-provider.h
 * @brief Cover-art fetching facade.
 *
 * Queries LastFM / Discogs / MusicBrainz (via async HTTP) for album
 * art.  Results are cached in a local SQLite store and emitted via
 * coverReady().  TODO — wire up actual API calls.
 */

#include <QObject>
#include <QUrl>
#include "cache-sqlite.h"

/// Facade that fetches cover art and caches results.
class CoverProvider : public QObject {
    Q_OBJECT
public:
    explicit CoverProvider(QObject *parent = nullptr);

    /// Start an async fetch for the given artist/album.
    void fetchCover(const QString &artist, const QString &album);

signals:
    /// Emitted when cover art data is ready (or cached).
    void coverReady(const QByteArray &data);

private:
    CacheSqlite cache_;   ///< Persistent cache for fetched covers.
};

