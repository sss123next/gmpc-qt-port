/**
 * @file async-download.cpp
 * @brief Implementation of the async HTTP downloader.
 *
 * Two-phase timeouts:
 *   connTimeout — fired if TCP+TLS handshake exceeds limit.
 *   xferTimeout — fired after headers received if response body is slow.
 */

#include "async-download.h"

#include <QTimer>
#include <QNetworkRequest>

QNetworkAccessManager *AsyncDownload::sharedManager()
{
    static QNetworkAccessManager *mgr = new QNetworkAccessManager();
    return mgr;
}

AsyncDownload::AsyncDownload(QObject *parent)
    : QObject(parent)
    , mgr_(sharedManager())
    , connTimer_(this)
    , xferTimer_(this)
{
    connTimer_.setSingleShot(true);
    xferTimer_.setSingleShot(true);
    connect(&connTimer_, &QTimer::timeout, this, &AsyncDownload::onConnTimeout);
    connect(&xferTimer_, &QTimer::timeout, this, &AsyncDownload::onXferTimeout);
}

void AsyncDownload::get(const QUrl &url, Callback cb)
{
    cancel();
    cb_ = std::move(cb);
    QNetworkRequest req(url);
    startRequest(req);
}

void AsyncDownload::get(const QNetworkRequest &req, Callback cb)
{
    cancel();
    cb_ = std::move(cb);
    QNetworkRequest copy(req);
    startRequest(copy);
}

void AsyncDownload::startRequest(const QNetworkRequest &req)
{
    current_ = mgr_->get(req);
    connect(current_, &QNetworkReply::finished,
            this, &AsyncDownload::onFinished);
    // When HTTP headers arrive, connection is established — switch timers.
    connect(current_, &QNetworkReply::metaDataChanged,
            this, &AsyncDownload::startXferTimer);
    startConnTimer();
}

void AsyncDownload::cancel()
{
    cb_ = {};
    connTimer_.stop();
    xferTimer_.stop();
    if (current_) {
        disconnect(current_, &QNetworkReply::finished,
                   this, &AsyncDownload::onFinished);
        disconnect(current_, &QNetworkReply::metaDataChanged,
                   this, &AsyncDownload::startXferTimer);
        current_->abort();
        current_->deleteLater();
        current_ = nullptr;
    }
}

void AsyncDownload::setTimeout(int connMs, int xferMs)
{
    connTimeoutMs_ = connMs;
    xferTimeoutMs_ = xferMs;
}

// ── Timer management ──────────────────────────────────────────────────────

void AsyncDownload::startConnTimer()
{
    connTimer_.stop();
    if (connTimeoutMs_ > 0)
        connTimer_.start(connTimeoutMs_);
}

void AsyncDownload::startXferTimer()
{
    connTimer_.stop();    // connection established
    if (xferTimeoutMs_ > 0)
        xferTimer_.start(xferTimeoutMs_);
}

void AsyncDownload::onConnTimeout()
{
    // Connection (TCP+TLS) took too long.
    abortWithError();
}

void AsyncDownload::onXferTimeout()
{
    // Headers received but response body is arriving too slowly.
    abortWithError();
}

void AsyncDownload::abortWithError()
{
    if (!cb_ || !current_) return;
    connTimer_.stop();
    xferTimer_.stop();
    current_->abort();           // triggers finished() which calls onFinished
}

// ── Completion ────────────────────────────────────────────────────────────

void AsyncDownload::onFinished()
{
    if (!cb_ || !current_) return;

    connTimer_.stop();
    xferTimer_.stop();

    QNetworkReply *const reply = current_;
    const bool ok = reply->error() == QNetworkReply::NoError;
    const QByteArray data = reply->readAll();

    // Fire callback — it may re-enter get(), replacing current_.
    cb_(ok, data);

    if (current_ == reply) {
        current_->deleteLater();
        current_ = nullptr;
    }
}
