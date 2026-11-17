#pragma once

/**
 * @file lyrics-ovh.h
 * @brief Backup lyrics provider via lyrics.ovh (no API key required).
 *
 * Simple REST API: GET https://api.lyrics.ovh/v1/artist/title
 * Returns JSON with a single "lyrics" field.
 * 404 = not found.
 *
 * Docs: https://lyricsovh.docs.apiary.io/
 */

#include <QObject>
#include <QString>
#include <QByteArray>
#include "tools/async-download.h"

/// Backup lyrics provider via lyrics.ovh.
class LyricsOvhProvider : public QObject {
    Q_OBJECT
public:
    explicit LyricsOvhProvider(QObject *parent = nullptr);

    /// Fetch plain-text lyrics by artist + track title.
    void fetchLyrics(const QString &artist, const QString &track);

signals:
    /// Emitted when lyrics are found (plain text only).
    void lyricsFound(const QString &plainLyrics);
    /// Emitted when lyrics are not found (404 or empty body).
    void notFound();
    /// Emitted on network error.
    void error(const QString &message);

public:   // ── Test seam (exposed for fuzz testing, no network) ──────────
    void testInjectReply(bool ok, const QByteArray &data) { onReply(ok, data); }

private:
    void onReply(bool ok, const QByteArray &data);

    AsyncDownload *dl_;
};
