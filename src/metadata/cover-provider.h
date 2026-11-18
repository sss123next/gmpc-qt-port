#pragma once

/**
 * @file cover-provider.h
 * @brief Cover-art fetching pipeline.
 *
 * Orchestrates cascading providers: MusicBrainz → Last.FM → Deezer → Discogs.
 * Results are cached in a local SQLite store.  Each invalid provider
 * (e.g. Last.FM without an API key) is skipped.
 *
 * Signals coverReady() or notFound() exactly once per fetchCover() call.
 */

#include <QObject>
#include <QByteArray>
#include "cache-sqlite.h"

class AsyncDownload;
class DiscogsProvider;

/// Facade that fetches cover art through a cascading provider chain.
class CoverProvider : public QObject {
    Q_OBJECT
public:
    /// @param lastFmApiKey  Empty string disables Last.FM.
    /// @param discogsToken  Empty string = unauthenticated (25 req/min).
    /// @param musicDir      MPD music directory (for local file lookup).  Empty = skip.
    explicit CoverProvider(const QString &lastFmApiKey,
                           const QString &discogsToken,
                           const QString &cachePath,
                           const QString &musicDir = QString(),
                           QObject *parent = nullptr);
    ~CoverProvider() override;

    /// Initiate an async cover fetch for artist/album.
    /// @param uri  Song file URI from MPD (relative to music dir), for local file lookup.
    /// @param skipCache  If true, bypass cache and force full provider pipeline.
    void fetchCover(const QString &artist, const QString &album,
                    const QString &uri = QString(), bool skipCache = false);

    /// Use a custom downloader (for test injection).
    void setDownloader(AsyncDownload *dl);
    /// Update music directory after construction (e.g. from MPD config).
    void setMusicDir(const QString &dir) { musicDir_ = dir; }

    // ── Test seams (exposed for fuzz testing) ──────────────────────────────
    /** @brief Inject artist/album so cacheKey() works without a prior
     *         fetchCover() call. */
    void testSetArtistAlbum(const QString &artist, const QString &album)
        { artist_ = artist; album_ = album; }
    /** @brief Feed a complete image directly (caches + emits coverReady).
     *  Must call testSetArtistAlbum() first. */
    void testInjectComplete(const QByteArray &data, const QString &label)
        { if (!data.isEmpty()) tryComplete(artist_, album_, data, label); }
    /** @brief Force a local file scan with given URI.  Needs musicDir_ set. */
    bool testTryLocalFile(const QString &uri) { return tryLocalFile(uri); }

signals:
    /// Emitted with the raw image data (JPEG/PNG) on success.
    void coverReady(const QByteArray &data);
    /// Emitted when all providers have been exhausted.
    void notFound();

private:
    void tryCache();
    void tryMusicBrainz();
    void onMBCover(const QByteArray &data);
    void onMBNotFound();
    void onMBError(const QString &msg);
    void tryLastFm();
    void onLFUrl(const QString &url, uint64_t expectedSeq,
                 const QString &reqArtist, const QString &reqAlbum);
    void onLFError(const QString &msg);
    void tryDeezer();
    void onDeezerError(const QString &msg);
    void tryDiscogs();
    void tryComplete(const QString &artist, const QString &album,
                     const QByteArray &data, const QString &label);

    /** @brief Check whether a callback captured at @p expectedSeq is still
     *         current.  Call at the top of every asynchronous callback;
     *         return immediately if stale. */
    bool isStale(uint64_t expectedSeq) const { return expectedSeq != seq_; }

    /** @brief Check for local cover files in the song's directory.
     *  Looks for cover.jpg, cover.png, folder.jpg, folder.png, album.jpg, album.png
     *  in the directory where the song resides (musicDir + dirname(uri)).
     *  @returns true if a local file was found and loaded. */
    bool tryLocalFile(const QString &uri);

    QString cacheKey() const;
    QString lastFmApiKey_;
    QString discogsToken_;
    CacheSqlite cache_;
    AsyncDownload *dl_;
    DiscogsProvider *discogs_;
    bool ownsDl_ = true;
    QString artist_;
    QString album_;
    QString musicDir_;   ///< MPD music directory from Config.
    uint64_t seq_ = 0;   ///< Monotonically increasing request serial.
};

// ── Deezer provider (standalone for testability) ────────────────────────────

/// Deezer cover-art provider (API-key-free).
class DeezerProvider : public QObject {
    Q_OBJECT
public:
    explicit DeezerProvider(QObject *parent = nullptr);
    ~DeezerProvider() override;

    /// Use a custom downloader (for test injection).
    void setDownloader(AsyncDownload *dl);

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
    bool ownsDl_ = true;
};
