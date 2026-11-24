/**
 * @file async-download.cpp
 * @brief Implementation of the async HTTP downloader.
 */

#include "async-download.h"

QNetworkAccessManager *AsyncDownload::sharedManager()
{
    static QNetworkAccessManager *mgr = new QNetworkAccessManager();
    return mgr;
}

AsyncDownload::AsyncDownload(QObject *parent)
    : QObject(parent)
    , mgr_(sharedManager())
{}

void AsyncDownload::get(const QUrl &url, Callback cb)
{
    cancel();
    cb_ = std::move(cb);
    QNetworkRequest req(url);
    applyTimeout(req);
    current_ = mgr_->get(req);
    connect(current_, &QNetworkReply::finished,
            this, &AsyncDownload::onFinished);
}

void AsyncDownload::get(const QNetworkRequest &req, Callback cb)
{
    cancel();
    cb_ = std::move(cb);
    QNetworkRequest copy(req);
    applyTimeout(copy);
    current_ = mgr_->get(copy);
    connect(current_, &QNetworkReply::finished,
            this, &AsyncDownload::onFinished);
}

void AsyncDownload::setTimeout(int ms)
{
    timeoutMs_ = ms;
}

void AsyncDownload::applyTimeout(QNetworkRequest &req)
{
    if (timeoutMs_ > 0)
        req.setTransferTimeout(timeoutMs_);
}

void AsyncDownload::cancel()
{
    // Clear callback BEFORE abort: abort() may emit finished() synchronously,
    // and onFinished() should skip the stale callback + reply.
    cb_ = {};
    if (current_) {
        // Disconnect first: the reply's finished signal may already be queued
        // in the event loop (QNAM worker thread → main thread).  Without this,
        // a stale queued event fires after current_ has been replaced by a
        // subsequent get() call, causing onFinished() to read from the wrong
        // reply.
        disconnect(current_, &QNetworkReply::finished,
                   this, &AsyncDownload::onFinished);
        current_->abort();
        current_->deleteLater();
        current_ = nullptr;
    }
}

void AsyncDownload::onFinished()
{
    if (!cb_ || !current_) return;

    QNetworkReply *const reply = current_;
    const bool ok = reply->error() == QNetworkReply::NoError;
    const QByteArray data = reply->readAll();

    // Call the callback — it may re-enter get(), replacing current_.
    cb_(ok, data);

    // Only clean up if current_ hasn't been replaced by a re-entrant get().
    if (current_ == reply) {
        current_->deleteLater();
        current_ = nullptr;
    }
}
