/**
 * @file async-download.cpp
 * @brief Implementation of the async HTTP downloader.
 */

#include "async-download.h"

AsyncDownload::AsyncDownload(QObject *parent)
    : QObject(parent)
    , mgr_(new QNetworkAccessManager(this))
{}

void AsyncDownload::get(const QUrl &url, Callback cb)
{
    cb_ = std::move(cb);
    QNetworkRequest req(url);
    current_ = mgr_->get(req);
    connect(current_, &QNetworkReply::finished,
            this, &AsyncDownload::onFinished);
}

void AsyncDownload::cancel()
{
    if (current_) {
        current_->abort();
        current_->deleteLater();
        current_ = nullptr;
    }
    cb_ = {};
}

void AsyncDownload::onFinished()
{
    if (!cb_ || !current_) return;
    if (current_->error() == QNetworkReply::NoError) {
        cb_(true, current_->readAll());
    } else {
        cb_(false, current_->readAll());
    }
    current_->deleteLater();
    current_ = nullptr;
}
