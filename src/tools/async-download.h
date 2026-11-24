#pragma once

/** @file async-download.h
 * @brief Minimal async HTTP downloader using QNetworkAccessManager.
 *
 * Supports separate connection and transfer timeouts via QTimer.
 * Used internally by CoverProvider and metadata providers.
 */

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QByteArray>
#include <QTimer>
#include <functional>

/// One-shot HTTP GET helper with callback on finish.
class AsyncDownload : public QObject {
    Q_OBJECT
public:
    /// Invoked when the download completes (or fails).
    using Callback = std::function<void(bool ok, const QByteArray &data)>;

    /// Extended callback that also provides HTTP status code and raw headers.
    using CallbackWithMeta = std::function<void(bool ok, const QByteArray &data,
                                                int httpStatusCode,
                                                const QByteArray &etag)>;

    explicit AsyncDownload(QObject *parent = nullptr);

    /// Start an HTTP GET request (plain URL).
    void get(const QUrl &url, Callback cb);

    /// Start an HTTP GET request with a pre-configured QNetworkRequest.
    void get(const QNetworkRequest &req, Callback cb);

    /// Start an HTTP GET with extended callback (status + ETag).
    void getMeta(const QNetworkRequest &req, CallbackWithMeta cb);

    /// Abort a running request (safe to call even if none is running).
    void cancel();

    /** @brief Set timeouts for subsequent requests.
     *  @param connTimeoutMs  Max time to establish TCP + TLS (ms).  0 = default.
     *  @param xferTimeoutMs  Max time after connection for full response (ms).  0 = default. */
    void setTimeout(int connTimeoutMs, int xferTimeoutMs);

private slots:
    void onFinished();
    void onConnTimeout();
    void onXferTimeout();

private:
    /// Shared QNetworkAccessManager — one instance for all HTTP downloads.
    static QNetworkAccessManager *sharedManager();
    void startRequest(const QNetworkRequest &req);
    void startConnTimer();
    void startXferTimer();
    void abortWithError();

    QNetworkAccessManager *mgr_;
    QNetworkReply *current_ = nullptr;
    Callback cb_;
    QTimer connTimer_;
    QTimer xferTimer_;
    int connTimeoutMs_ = 0;
    int xferTimeoutMs_ = 0;
};

