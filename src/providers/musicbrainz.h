#pragma once

/**
 * @file musicbrainz.h
 * @brief MusicBrainz / Cover Art Archive provider.
 *
 * Two-phase lookup:
 *   1. MusicBrainz API — search release by artist+album, get MBID.
 *   2. Cover Art Archive — fetch cover image by release MBID.
 *
 * Neither requires an API key.
 *
 * Rate limits (MusicBrainz):
 *   - 1 req/sec (use internal throttling).
 *   - Must supply a User-Agent header identifying the application.
 *
 * Cover Art Archive is unauthenticated, no documented rate limit.
 */

#include <QObject>
#include <QString>
#include <QByteArray>
#include "tools/async-download.h"

/// Cover-art provider via MusicBrainz → Cover Art Archive.
class MusicBrainzProvider : public QObject {
    Q_OBJECT
public:
    explicit MusicBrainzProvider(QObject *parent = nullptr);

    /// Fetch cover image bytes for artist/album.
    void fetchCover(const QString &artist, const QString &album);

signals:
    /// Emitted with raw cover image data (JPEG/PNG).
    void coverFound(const QByteArray &imageData);
    /// Emitted when cover not found in any available archive.
    void notFound();
    /// Emitted on network or API error.
    void error(const QString &message);

private:
    void onMBSearchReply(bool ok, const QByteArray &data);
    void onCAAReply(bool ok, const QByteArray &data);
    void fetchCAAImage(const QString &releaseMbid);

    AsyncDownload *dl_;
    QString pendingMbid_;
};

