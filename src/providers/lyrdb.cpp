/**
 * @file lyrdb.cpp
 * @brief LRCLIB lyrics provider implementation.
 *
 * Parses the JSON response from LRCLIB and emits either
 * plain lyrics or synced LRC lyrics.
 */

#include "lyrdb.h"
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>

static const char *kApiBase = "https://lrclib.net/api";

LrcLibProvider::LrcLibProvider(QObject *parent)
    : QObject(parent), dl_(new AsyncDownload(this))
{
    dl_->setTimeout(10000);  // 10s overall timeout for lyrics downloads
}

void LrcLibProvider::fetchLyrics(const QString &artist, const QString &track,
                                 const QString &album, int durationSec)
{
    QUrl url(QString("%1/get").arg(kApiBase));
    QUrlQuery q;
    q.addQueryItem("artist_name", artist);
    q.addQueryItem("track_name", track);
    if (!album.isEmpty())
        q.addQueryItem("album_name", album);
    url.setQuery(q);

    Q_UNUSED(durationSec);

    dl_->get(url, [this](bool ok, const QByteArray &data) {
        onReply(ok, data);
    });
}

void LrcLibProvider::onReply(bool ok, const QByteArray &data)
{
    if (!ok) {
        emit error(tr("Network error fetching lyrics from LRCLIB"));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        emit error(tr("LRCLIB: invalid JSON"));
        return;
    }

    QJsonObject obj = doc.object();
    if (obj.isEmpty() || obj.value("error").isString()) {
        emit notFound();
        return;
    }

    QString plain = obj.value("plainLyrics").toString();
    QString synced = obj.value("syncedLyrics").toString();

    if (plain.isEmpty() && synced.isEmpty()) {
        emit notFound();
        return;
    }

    emit lyricsFound(plain, synced);
}
