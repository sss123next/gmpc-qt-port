#pragma once

/**
 * @file metadata-manager.h
 * @brief Orchestrator for metadata and cover-art retrieval.
 *
 * Coordinates CoverProvider and lyric fetchers.  Clients connect
 * to coverReady() / lyricsReady() and call requestCover() / requestLyrics().
 *
 * Implicitly shares the AsyncDownload instance across sub-providers
 * so that concurrent requests are serialised per-manager.
 */

#include <QObject>
#include <QByteArray>
#include <QString>

class CoverProvider;
class LrcLibProvider;
class LyricsOvhProvider;
class CacheSqlite;

/// Central coordinator for metadata fetching and caching.
class MetadataManager : public QObject {
    Q_OBJECT
public:
    /// @param cachePath     Path to the SQLite cache database.
    /// @param lastFmApiKey  API key for Last.FM (empty = disabled).
    /// @param discogsToken  Discogs personal access token (empty = unauthenticated).
    explicit MetadataManager(const QString &cachePath,
                             const QString &lastFmApiKey,
                             const QString &discogsToken,
                             QObject *parent = nullptr);

    ~MetadataManager() override;

    /// Start a cover-art fetch.  May return cached data immediately.
    void requestCover(const QString &artist, const QString &album);

    /// Start a lyrics fetch.
    void requestLyrics(const QString &artist, const QString &title);

signals:
    /// Emitted when cover image data is ready.
    void coverReady(const QString &artist, const QString &album,
                    const QByteArray &data);
    /// Emitted when cover not found in any provider.
    void coverNotFound(const QString &artist, const QString &album);

    /// Emitted when lyrics text is ready.
    void lyricsReady(const QString &artist, const QString &title,
                     const QString &lyrics);
    /// Emitted when lyrics not found.
    void lyricsNotFound(const QString &artist, const QString &title);

    /// Emitted on any provider error (for logging / UI).
    void error(const QString &message);

private:
    void onCoverData(const QString &artist, const QString &album,
                     const QByteArray &data);
    void onCoverNotFound(const QString &artist, const QString &album);
    void cachePut(const QString &key, const QByteArray &data);

    CoverProvider    *coverProvider_;
    LrcLibProvider   *lyricsProvider_;
    LyricsOvhProvider *lyricsFallback_;
    CacheSqlite      *cache_;

    // Pending request state for signal forwarding
    QString pendingArtist_;
    QString pendingAlbum_;
    QString pendingArtistLyrics_;
    QString pendingTitleLyrics_;
};

// ── Forward-declare external providers ──────────────────────────────────────
#include "tools/async-download.h"
#include "providers/lyrdb.h"
#include "providers/lyrics-ovh.h"
