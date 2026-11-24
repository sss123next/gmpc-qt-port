#pragma once

/**
 * @file lrcmux.h
 * @brief Lrcmux aggregated lyrics provider (free, no API key).
 *
 * Aggregates Deezer, KuGou, YouTube Music and other providers into a single
 * endpoint.  Returns word/line-level synced lyrics as JSON, LRC, or plain text.
 *
 * API docs: https://lrcmux.dev/docs
 */

#include <QObject>
#include <QString>
#include <QByteArray>
#include "tools/async-download.h"

/// Lyrics provider via lrcmux.dev (free, no API key).
class LrcmuxProvider : public QObject {
    Q_OBJECT
public:
    explicit LrcmuxProvider(QObject *parent = nullptr);

    /// Fetch lyrics by artist + track name (duration in seconds, optional).
    void fetchLyrics(const QString &artist, const QString &track,
                     int durationSec = 0);

signals:
    /// Emitted when lyrics are found (plain text, synced LRC-like string).
    void lyricsFound(const QString &plainLyrics,
                     const QString &syncedLyrics);
    /// Emitted when lyrics are not found.
    void notFound();
    /// Emitted on network error.
    void error(const QString &message);

public:   // ── Test seams (exposed for fuzz testing, no network) ──────────
    void testInjectReply(bool ok, const QByteArray &data)
        { onReply(ok, data); }

private:
    void onReply(bool ok, const QByteArray &data);
    /// Build LRC-style string from JSON lines with word/line timestamps.
    static QString buildLrc(const QJsonArray &lines, const QString &level);

    AsyncDownload *dl_;
    QString searchedArtist_;
    QString searchedTrack_;
    int searchedDuration_ = 0;
};
