/**
 * @file lyrics-ovh.cpp
 * @brief lyrics.ovh provider implementation.
 *
 * Calls GET https://api.lyrics.ovh/v1/{artist}/{title}
 * and parses the JSON response.  Emits lyricsFound() on success,
 * notFound() on 404 or empty lyrics, error() on network failure.
 */

#include "lyrics-ovh.h"
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>

static const char *kApiBase = "https://api.lyrics.ovh/v1";

LyricsOvhProvider::LyricsOvhProvider(QObject *parent)
    : QObject(parent), dl_(new AsyncDownload(this))
{
}

void LyricsOvhProvider::fetchLyrics(const QString &artist, const QString &track)
{
    QString path = QString("%1/%2/%3")
        .arg(kApiBase,
             QString::fromUtf8(QUrl::toPercentEncoding(artist)),
             QString::fromUtf8(QUrl::toPercentEncoding(track)));

    QUrl url(path);

    dl_->get(url, [this](bool ok, const QByteArray &data) {
        onReply(ok, data);
    });
}

void LyricsOvhProvider::onReply(bool ok, const QByteArray &data)
{
    if (!ok) {
        emit error(tr("Network error fetching lyrics from lyrics.ovh"));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        emit error(tr("lyrics.ovh: invalid JSON"));
        return;
    }

    QJsonObject obj = doc.object();

    // 404 returns {"error":"lyrics not found"}, so check for "lyrics" key
    if (!obj.contains("lyrics")) {
        emit notFound();
        return;
    }

    QString lyrics = obj.value("lyrics").toString();
    if (lyrics.isEmpty()) {
        emit notFound();
        return;
    }

    emit lyricsFound(lyrics);
}
