#pragma once

/**
 * @file lrc-red.h
 * @brief lrc.red synchronized lyrics provider.
 *
 * Fetches word/syllable-level synced lyrics from the lrc.red community database.
 * No API key required. Uses ISRC-based lookup after title+artist search.
 *
 * API docs: https://lrc.red/api
 * Terms: free, must credit lrc.red, must send User-Agent.
 */

#include <QObject>
#include <QString>
#include <QByteArray>
#include "tools/async-download.h"

/// Lyrics provider via lrc.red (free, no API key).
class LrcRedProvider : public QObject {
    Q_OBJECT
public:
    explicit LrcRedProvider(QObject *parent = nullptr);

    /// Fetch lyrics by artist + track name (duration in seconds, ±3s tolerance).
    void fetchLyrics(const QString &artist, const QString &track,
                     int durationSec = 0);

signals:
    /// Emitted when lyrics are found (plain text, synced TTML/LRC).
    void lyricsFound(const QString &plainLyrics,
                     const QString &syncedLyrics);
    /// Emitted when lyrics are not found.
    void notFound();
    /// Emitted on network error.
    void error(const QString &message);

public:   // ── Test seams (exposed for fuzz testing, no network) ──────────
    void testInjectSearchReply(bool ok, const QByteArray &data)
        { onSearchReply(ok, data); }
    void testInjectTrackReply(bool ok, const QByteArray &data)
        { onTrackReply(ok, data); }
    void testInjectLyricsReply(bool ok, const QByteArray &data)
        { onLyricsReply(ok, data); }

private:
    void onSearchReply(bool ok, const QByteArray &data);
    void onTrackReply(bool ok, const QByteArray &data);
    void onLyricsReply(bool ok, const QByteArray &data);

    /// Strip TTML tags to get plain text.
    static QString ttmlToPlain(const QString &ttml);

    AsyncDownload *dl_;
    QString searchedArtist_;
    QString searchedTrack_;
    int searchedDuration_ = 0;
};
