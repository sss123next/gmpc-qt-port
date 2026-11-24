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
class LrcRedProvider;
class LrcmuxProvider;
class UnisonProvider;
class LyricsOvhProvider;
class CacheSqlite;

/// Central coordinator for metadata fetching and caching.
class MetadataManager : public QObject {
    Q_OBJECT
public:
    /// @param cachePath     Path to the SQLite cache database.
    /// @param lastFmApiKey  API key for Last.FM (empty = disabled).
    /// @param discogsToken  Discogs personal access token (empty = unauthenticated).
    /// @param musicDir      MPD music directory (for local cover file lookup). Empty = skip.
    explicit MetadataManager(const QString &cachePath,
                             const QString &lastFmApiKey,
                             const QString &discogsToken,
                             const QString &musicDir = QString(),
                             QObject *parent = nullptr);

    ~MetadataManager() override;

    /** @brief Start a cover-art fetch.  May return cached data immediately.
     *  @param uri  Song file URI from MPD (relative to music dir), for local file lookup.
     *              Empty string skips local file search. */
    void requestCover(const QString &artist, const QString &album,
                      const QString &uri = QString());

    /// Start a lyrics fetch.
    /// @param uri  Song file URI from MPD, for embedded-tag lookup. Empty = skip.
    void requestLyrics(const QString &artist, const QString &title,
                       const QString &uri = QString());

    /** @brief Update music directory for local cover file lookup.
     *  Passes through to CoverProvider::setMusicDir(). */
    void setMusicDir(const QString &dir);

    /** @brief Force re-fetch cover: invalidates cache and runs full provider pipeline.
     *  Emits coverReady/coverNotFound when done. */
    void refreshCover(const QString &artist, const QString &album,
                      const QString &uri = QString());
    /** @brief Force re-fetch lyrics: invalidates cache and runs full provider pipeline.
     *  Emits lyricsReady/lyricsNotFound when done. */
    void refreshLyrics(const QString &artist, const QString &title,
                       const QString &uri = QString());

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

    /// Emitted when Unison provides attribution info (must credit in UI).
    void lyricsAttribution(const QString &unisonUrl,
                           const QString &submitterName,
                           const QString &submitterUrl);

private:
    void onCoverData(const QString &artist, const QString &album,
                     const QByteArray &data);
    void onCoverNotFound(const QString &artist, const QString &album);
    void cachePut(const QString &key, const QByteArray &data);

    CoverProvider    *coverProvider_;
    LrcLibProvider   *lyricsProvider_;
    LrcRedProvider   *lrcRedProvider_;
    LrcmuxProvider   *lrcmuxProvider_;
    UnisonProvider   *unisonProvider_;
    LyricsOvhProvider *lyricsFallback_;
    CacheSqlite      *cache_;
    QString           musicDir_;

    // Pending request state for signal forwarding
    QString pendingArtist_;
    QString pendingAlbum_;
    QString pendingArtistLyrics_;
    QString pendingTitleLyrics_;

    // Refresh-in-progress flags: suppress "not found" emissions on failure
    // so existing cache and display are preserved when re-fetch yields nothing.
    bool refreshingCover_ = false;
    bool refreshingLyrics_ = false;
};

// ── Forward-declare external providers ──────────────────────────────────────
#include "tools/async-download.h"
#include "providers/lyrdb.h"
#include "providers/lrc-red.h"
#include "providers/lrcmux.h"
#include "providers/unison.h"
#include "providers/lyrics-ovh.h"
