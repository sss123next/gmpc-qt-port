/**
 * @file lastfm.cpp
 * @brief Last.FM API cover-art provider.
 *
 * Calls album.getInfo, extracts the largest image URL from the
 * JSON response, and emits coverFound().
 */

#include "lastfm.h"
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

static const char *kApiBase = "https://ws.audioscrobbler.com/2.0/";

LastFmProvider::LastFmProvider(const QString &apiKey, QObject *parent)
    : QObject(parent), apiKey_(apiKey), dl_(new AsyncDownload(this))
{
}

void LastFmProvider::fetchCover(const QString &artist, const QString &album)
{
    QUrl url(kApiBase);
    QUrlQuery q;
    q.addQueryItem("method", "album.getInfo");
    q.addQueryItem("api_key", apiKey_);
    q.addQueryItem("artist", artist);
    q.addQueryItem("album", album);
    q.addQueryItem("format", "json");
    url.setQuery(q);

    dl_->get(url, [this](bool ok, const QByteArray &data) {
        onReply(ok, data);
    });
}

void LastFmProvider::onReply(bool ok, const QByteArray &data)
{
    if (!ok) {
        emit error(tr("Network error fetching cover from Last.FM"));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        emit error(tr("Last.FM: invalid JSON"));
        return;
    }

    QJsonObject root = doc.object();
    if (root.contains("error")) {
        emit error(root.value("message").toString());
        return;
    }

    QJsonObject album = root.value("album").toObject();
    QJsonArray images = album.value("image").toArray();
    QString bestUrl;
    for (const auto &img : images) {
        QJsonObject imgObj = img.toObject();
        QString url = imgObj.value("#text").toString();
        if (!url.isEmpty())
            bestUrl = url;
    }

    if (!bestUrl.isEmpty())
        emit coverFound(bestUrl);
    else
        emit error(tr("Last.FM: no cover image found"));
}
