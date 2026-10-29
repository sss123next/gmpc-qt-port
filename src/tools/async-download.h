#ifndef GMPCQT_ASYNC_DOWNLOAD_H
#define GMPCQT_ASYNC_DOWNLOAD_H

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

    /// Start an HTTP GET request.
    void get(const QUrl &url, Callback cb);

    /// Abort a running request (safe to call even if none is running).
    void cancel();

private slots:
    void onFinished();

private:
    QNetworkAccessManager *mgr_;
    QNetworkReply *current_ = nullptr;
    Callback cb_;
};

#endif // GMPCQT_ASYNC_DOWNLOAD_H
