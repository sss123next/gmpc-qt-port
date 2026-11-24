/**
 * @file discogs.cpp
 * @brief Discogs API cover-art provider.
 *
 * Search → pick first result → download cover_image URL.
 */

#include "discogs.h"

#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(discogsLog, "discogs", QtWarningMsg)

static const char *kSearchUrl = "https://api.discogs.com/database/search";

DiscogsProvider::DiscogsProvider(const QString &token, QObject *parent)
    : QObject(parent)
    , token_(token)
    , dl_(new AsyncDownload(this))
{
    dl_->setTimeout(5000, 30000);  // 5s connect, 30s overall for covers
    if (token_.isEmpty())
        qCWarning(discogsLog) << "Discogs: no token — rate limited to 25 req/min";
}

DiscogsProvider::~DiscogsProvider()
{
    if (!ownsDl_) dl_ = nullptr;
}

void DiscogsProvider::setDownloader(AsyncDownload *dl)
{
    if (ownsDl_ && dl_) delete dl_;
    dl_ = dl;
    ownsDl_ = false;
}

void DiscogsProvider::cancel()
{
    dl_->cancel();
}

void DiscogsProvider::fetchCover(const QString &artist, const QString &album)
{
    QUrl url(kSearchUrl);
    QUrlQuery q;
    q.addQueryItem("q", QString("%1 %2").arg(artist, album));
    q.addQueryItem("type", "release");
    if (!token_.isEmpty())
        q.addQueryItem("token", token_);
    url.setQuery(q);

    dl_->get(url, [this](bool ok, const QByteArray &data) {
        onSearchReply(ok, data);
    });
}

void DiscogsProvider::onSearchReply(bool ok, const QByteArray &data)
{
    if (!ok) {
        emit error(tr("Discogs: network error"));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        emit error(tr("Discogs: invalid JSON"));
        return;
    }

    QJsonObject root = doc.object();
    if (root.contains("message")) {
        emit error(root.value("message").toString());
        return;
    }

    QJsonArray results = root.value("results").toArray();
    if (results.isEmpty()) {
        emit notFound();
        return;
    }

    // Pick first result's cover_image
    QString coverUrl = results[0].toObject().value("cover_image").toString();
    if (coverUrl.isEmpty()) {
        emit notFound();
        return;
    }

    dl_->get(QUrl(coverUrl), [this](bool ok2, const QByteArray &data2) {
        onImageReply(ok2, data2);
    });
}

void DiscogsProvider::onImageReply(bool ok, const QByteArray &data)
{
    if (!ok || data.isEmpty()) {
        emit error(tr("Discogs: image download failed"));
        return;
    }
    emit coverFound(data);
}
