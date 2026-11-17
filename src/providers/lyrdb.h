#pragma once

/**
 * @file lyrdb.h
 * @brief LRCLIB lyrics provider.
 *
 * Fetches synchronised and plain-text lyrics from LRCLIB.
 * No API key required.  Supports duration-based matching and
 * MusicBrainz recording ID lookup.
 *
 * Primary endpoint:
 * @code
 *   GET /api/get?artist=ARTIST&track=TRACK&album_name=ALBUM (opt)
 *   GET /api/get?musicbrainz_track_id=MBID
 * @endcode
 *
 * Rate limit: ~45-180 req/min depending on endpoint.
 * Docs: https://lrclib.net/docs
 */

#include <QObject>
#include <QString>
#include <QByteArray>
#include "tools/async-download.h"

/// Lyrics provider via LRCLIB (no API key required).
class LrcLibProvider : public QObject {
    Q_OBJECT
public:
    explicit LrcLibProvider(QObject *parent = nullptr);

    /// Fetch lyrics by artist + track name (duration optional, for sync).
    void fetchLyrics(const QString &artist, const QString &track,
                     const QString &album = {}, int durationSec = 0);

signals:
    /// Emitted when lyrics are found (plain text or synced LRC).
    void lyricsFound(const QString &plainLyrics,
                     const QString &syncedLyrics);
    /// Emitted when lyrics are not found.
    void notFound();
    /// Emitted on network error.
    void error(const QString &message);

public:   // ── Test seam (exposed for fuzz testing, no network) ──────────
    void testInjectReply(bool ok, const QByteArray &data) { onReply(ok, data); }

private:
    void onReply(bool ok, const QByteArray &data);

    AsyncDownload *dl_;
};

