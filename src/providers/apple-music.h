#pragma once

/**
 * @file apple-music.h
 * @brief Apple Music / iTunes Search API cover-art provider (no API key required).
 *
 * Fetches album artwork from Apple's public iTunes Search API.  No registration
 * or API key is needed.  Searches by artist+album name, matches on artist name,
 * then downloads the cover image at configurable resolution (default 600×600).
 *
 * API docs: https://developer.apple.com/library/archive/documentation/AudioVideo/Conceptual/iTuneSearchAPI/
 */

#include <QObject>
#include <QByteArray>
#include <QString>

class AsyncDownload;

/// Apple Music / iTunes Search cover provider (free, no key).
class AppleMusicProvider : public QObject {
    Q_OBJECT
public:
    explicit AppleMusicProvider(QObject *parent = nullptr);
    ~AppleMusicProvider() override;

    /// Use a custom downloader (for test injection).
    void setDownloader(AsyncDownload *dl);

    /// Search for album cover by artist + album name.
    void fetchCover(const QString &artist, const QString &album,
                    int targetSizePx = 600);

    /// Replace size suffix in artwork URL (e.g. 100x100bb → 600x600bb).
    static QString resizeArtworkUrl(const QString &url, int px);

signals:
    void coverFound(const QByteArray &imageData);
    void notFound();
    void error(const QString &message);

public:   // ── Test seams (exposed for fuzz testing, no network) ──────────
    void testInjectSearchReply(bool ok, const QByteArray &data)
        { onSearchReply(ok, data); }
    void testInjectImageReply(bool ok, const QByteArray &data)
        { onImageReply(ok, data); }
    /// Set the searched artist/album (needed before testInjectSearchReply).
    void testSetSearched(const QString &artist, const QString &album)
        { searchedArtist_ = artist; searchedAlbum_ = album; }

private:
    void onSearchReply(bool ok, const QByteArray &data);
    void onImageReply(bool ok, const QByteArray &data);

    AsyncDownload *dl_;
    bool ownsDl_ = true;
    int targetSize_ = 600;
    QString searchedArtist_;
    QString searchedAlbum_;
};
