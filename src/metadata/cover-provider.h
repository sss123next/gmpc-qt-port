#pragma once

/**
 * @file cover-provider.h
 * @brief Cover-art fetching pipeline.
 *
 * Orchestrates cascading providers: MusicBrainz → Last.FM → Deezer.
 * Results are cached in a local SQLite store.  Each invalid provider
 * (e.g. Last.FM without an API key) is skipped.
 *
 * Signals coverReady() or notFound() exactly once per fetchCover() call.
 */

#include <QObject>
#include <QByteArray>
#include "cache-sqlite.h"

class AsyncDownload;

/// Facade that fetches cover art through a cascading provider chain.
class CoverProvider : public QObject {
    Q_OBJECT
public:
    /// @param lastFmApiKey Empty string disables Last.FM.
    explicit CoverProvider(const QString &lastFmApiKey,
                           const QString &cachePath,
                           QObject *parent = nullptr);
    ~CoverProvider() override;

    /// Initiate an async cover fetch for artist/album.
    void fetchCover(const QString &artist, const QString &album);

    /// Use a custom downloader (for test injection).
    void setDownloader(AsyncDownload *dl);

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
    void onLFUrl(const QString &url);
    void onLFError(const QString &msg);
    void tryDeezer();
    void onDeezerCover(const QByteArray &data);
    void onDeezerError(const QString &msg);
    void tryComplete(const QByteArray &data, const QString &label);

    QString cacheKey() const;
    QString lastFmApiKey_;
    CacheSqlite cache_;
    AsyncDownload *dl_;
    bool ownsDl_ = true;
    QString artist_;
    QString album_;
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

private:
    void onSearchReply(bool ok, const QByteArray &data);
    void onImageReply(bool ok, const QByteArray &data);
    AsyncDownload *dl_;
    bool ownsDl_ = true;
};
