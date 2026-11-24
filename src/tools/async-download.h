#pragma once

/**
 * @file async-download.h
 * @brief Minimal async HTTP downloader using QNetworkAccessManager.
 *
 * Used internally by CoverProvider and metadata providers to fetch
 * album art, lyrics, and other remote resources.
 */

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QByteArray>
#include <functional>

/// One-shot HTTP GET helper with callback on finish.
class AsyncDownload : public QObject {
    Q_OBJECT
public:
    /// Invoked when the download completes (or fails).
    using Callback = std::function<void(bool ok, const QByteArray &data)>;

    explicit AsyncDownload(QObject *parent = nullptr);

    /// Start an HTTP GET request (plain URL).
    void get(const QUrl &url, Callback cb);

    /// Start an HTTP GET request with a pre-configured QNetworkRequest.
    void get(const QNetworkRequest &req, Callback cb);

    /// Abort a running request (safe to call even if none is running).
    void cancel();

    /** @brief Set overall transfer timeout (connect + read) for subsequent
     *         requests.  0 = no timeout (Qt default). */
    void setTimeout(int ms);

private slots:
    void onFinished();

private:
    /// Shared QNetworkAccessManager — one instance for all HTTP downloads.
    static QNetworkAccessManager *sharedManager();
    void applyTimeout(QNetworkRequest &req);
    QNetworkAccessManager *mgr_;
    QNetworkReply *current_ = nullptr;
    Callback cb_;
    int timeoutMs_ = 0;
};

