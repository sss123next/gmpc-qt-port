#pragma once

/**
 * @file unison.h
 * @brief Unison community-synced lyrics provider (free for open-source).
 *
 * Fetches TTML/LRC/plain lyrics from the Unison community database.
 * Free for open-source projects (gmpc-qt is GPLv2).  No API key required.
 * Responses carry ETag for conditional re-fetching.
 * Attribution required when displaying lyrics.
 *
 * API docs: https://unison.betterlyrics.org/docs
 * Terms: free for open-source, must credit Unison + submitter.
 */

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QHash>
#include "tools/async-download.h"

/// Lyrics provider via Unison (free for open-source, no API key).
class UnisonProvider : public QObject {
    Q_OBJECT
public:
    explicit UnisonProvider(QObject *parent = nullptr);

    /// Fetch lyrics by artist + track name (album and duration optional).
    void fetchLyrics(const QString &artist, const QString &track,
                     const QString &album = {}, int durationSec = 0);

signals:
    /// Emitted when lyrics are found (plain text, synced lyrics in original format).
    void lyricsFound(const QString &plainLyrics,
                     const QString &syncedLyrics);
    /// Emitted when lyrics are not found.
    void notFound();
    /// Emitted on network error.
    void error(const QString &message);

    /// Emitted with attribution info when lyrics are found.
    void attributionInfo(const QString &unisonUrl,
                         const QString &submitterName,
                         const QString &submitterUrl);

public:   // ── Test seams (exposed for fuzz testing, no network) ──────────
    void testInjectReply(bool ok, const QByteArray &data)
        { onReply(ok, data, 200, QByteArray()); }

private:
    void onReply(bool ok, const QByteArray &data, int httpStatus,
                 const QByteArray &etag);
    void onMetaReply(bool ok, const QByteArray &data, int httpStatus,
                     const QByteArray &etag);

    /// Extract plain text from various lyrics formats.
    static QString extractPlain(const QString &lyrics, const QString &format);

    /** @brief Build a cache key for ETag storage (artist + track). */
    static QString etagKey(const QString &artist, const QString &track);

    AsyncDownload *dl_;
    QString searchedArtist_;
    QString searchedTrack_;
    QString searchedAlbum_;
    int searchedDuration_ = 0;
    /// Per-track ETag cache: key = "artist\x00track", value = ETag string.
    QHash<QString, QString> etagCache_;
};
