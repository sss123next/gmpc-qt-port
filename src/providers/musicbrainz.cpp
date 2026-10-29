/**
 * @file musicbrainz.cpp
 * @brief MusicBrainz / Cover Art Archive provider implementation.
 */

#include "musicbrainz.h"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

static const char *kMBSearchUrl   = "https://musicbrainz.org/ws/2/release/";
static const char *kCAAUrl        = "https://coverartarchive.org/release/";
static const char *kUserAgent     = "GMPC-Qt/0.1 (LLM port)";

MusicBrainzProvider::MusicBrainzProvider(QObject *parent)
    : QObject(parent), dl_(new AsyncDownload(this))
{
}

void MusicBrainzProvider::fetchCover(const QString &artist, const QString &album)
{
    QUrl url(kMBSearchUrl);
    QUrlQuery q;
    q.addQueryItem("query", QString("artist:\"%1\" AND release:\"%2\"")
                                .arg(artist, album));
    q.addQueryItem("fmt", "json");
    q.addQueryItem("limit", "1");
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", kUserAgent);
    req.setRawHeader("Accept", "application/json");

    dl_->get(req, [this](bool ok, const QByteArray &data) {
        onMBSearchReply(ok, data);
    });
}

void MusicBrainzProvider::onMBSearchReply(bool ok, const QByteArray &data)
{
    if (!ok) {
        emit error(tr("MusicBrainz: network error"));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        emit error(tr("MusicBrainz: invalid JSON"));
        return;
    }

    QJsonArray releases = doc.object().value("releases").toArray();
    if (releases.isEmpty()) {
        emit notFound();
        return;
    }

    QString mbid = releases[0].toObject().value("id").toString();
    if (mbid.isEmpty()) {
        emit notFound();
        return;
    }

    fetchCAAImage(mbid);
}

void MusicBrainzProvider::fetchCAAImage(const QString &releaseMbid)
{
    QUrl url(QString("%1%2/front").arg(kCAAUrl, releaseMbid));
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", kUserAgent);

    dl_->get(req, [this](bool ok, const QByteArray &data) {
        onCAAReply(ok, data);
    });
}

void MusicBrainzProvider::onCAAReply(bool ok, const QByteArray &data)
{
    if (!ok) {
        emit error(tr("Cover Art Archive: not found or network error"));
        return;
    }

    if (data.isEmpty()) {
        emit notFound();
        return;
    }

    emit coverFound(data);
}
