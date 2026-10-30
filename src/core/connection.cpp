/**
 * @file connection.cpp
 * @brief Implementation of the MPD Connection wrapper.
 */

#include "connection.h"
#include "tools/log.h"

#include <cstring>

Connection::Connection(QObject *parent)
    : QObject(parent)
    , pollTimer_(new QTimer(this))
{
    pollTimer_->setInterval(kDefaultPollMs);
    connect(pollTimer_, &QTimer::timeout,
            this, &Connection::pollStatus);
}

Connection::~Connection()
{
    disconnect();
}

bool Connection::connectToServer(const QString &host, int port,
                                  const QString &password, int timeoutMs)
{
    if (conn_)
        disconnect();

    conn_ = mpd_connection_new(host.toUtf8().constData(), port, timeoutMs);
    if (!conn_) {
        emit errorOccurred(tr("Failed to create MPD connection object"));
        return false;
    }

    if (mpd_connection_get_error(conn_) != MPD_ERROR_SUCCESS) {
        QString err = QString::fromUtf8(
            mpd_connection_get_error_message(conn_));
        mpd_connection_free(conn_);
        conn_ = nullptr;
        emit errorOccurred(err);
        return false;
    }

    if (!password.isEmpty()) {
        if (!mpd_run_password(conn_,
                              password.toUtf8().constData()))
        {
            QString err = QString::fromUtf8(
                mpd_connection_get_error_message(conn_));
            mpd_connection_free(conn_);
            conn_ = nullptr;
            emit errorOccurred(err);
            return false;
        }
    }

    LOG_INFO("Connected to MPD at %s:%d",
             qPrintable(host), port);
    pollTimer_->start();
    emit connected();
    return true;
}

void Connection::disconnect()
{
    pollTimer_->stop();
    if (conn_) {
        mpd_connection_free(conn_);
        conn_ = nullptr;
    }
    lastStatus_ = Status{};
    emit disconnected();
}

bool Connection::isConnected() const
{
    return conn_ != nullptr
        && mpd_connection_get_error(conn_) == MPD_ERROR_SUCCESS;
}

Connection::Status Connection::fetchStatus()
{
    Status s;
    if (!conn_)
        return s;

    mpd_status *status = mpd_run_status(conn_);
    if (!status)
        return s;

    s.state         = mpd_status_get_state(status);
    s.volume        = mpd_status_get_volume(status);
    s.songId        = mpd_status_get_song_id(status);
    s.songPos       = mpd_status_get_song_pos(status);
    s.queueLength   = mpd_status_get_queue_length(status);
    s.queueVersion  = mpd_status_get_queue_version(status);
    s.elapsed       = mpd_status_get_elapsed_time(status);
    s.totalDuration = mpd_status_get_total_time(status);
    s.bitRate       = mpd_status_get_kbit_rate(status);
    s.repeat        = mpd_status_get_repeat(status);
    s.random        = mpd_status_get_random(status);
    s.single        = mpd_status_get_single(status);
    s.consume       = mpd_status_get_consume(status);
    s.crossfade     = mpd_status_get_crossfade(status);

    mpd_status_free(status);
    return s;
}

mpd_entity *Connection::nextEntity()
{
    return conn_ ? mpd_recv_entity(conn_) : nullptr;
}

QList<QPair<QString, QString>> Connection::rawCommand(const QString &cmd)
{
    QList<QPair<QString, QString>> result;
    if (!conn_)
        return result;

    // Send the command
    if (!mpd_send_command(conn_, cmd.toUtf8().constData(), nullptr)) {
        LOG_WARN("rawCommand(%s): mpd_send_command failed",
                 qPrintable(cmd));
        mpd_connection_clear_error(conn_);
        return result;
    }

    // Read all response pairs
    while (auto *pair = mpd_recv_pair(conn_)) {
        result.append({ QString::fromUtf8(pair->name),
                        QString::fromUtf8(pair->value) });
        mpd_return_pair(conn_, pair);
    }

    // Check for errors during iteration
    if (mpd_connection_get_error(conn_) != MPD_ERROR_SUCCESS
        && mpd_connection_get_error(conn_) != MPD_ERROR_CLOSED)
    {
        LOG_WARN("rawCommand(%s): error during recv",
                 qPrintable(cmd));
        mpd_connection_clear_error(conn_);
        result.clear();
        return result;
    }

    // Finish the response (consumes ACK if any)
    if (!mpd_response_finish(conn_)) {
        LOG_WARN("rawCommand(%s): mpd_response_finish failed",
                 qPrintable(cmd));
        mpd_connection_clear_error(conn_);
        result.clear();
        return result;
    }

    return result;
}

void Connection::pollStatus()
{
    if (!isConnected()) {
        pollTimer_->stop();
        emit disconnected();
        return;
    }

    Status s = fetchStatus();
    bool changed = (s.state        != lastStatus_.state        ||
                    s.volume       != lastStatus_.volume       ||
                    s.songId       != lastStatus_.songId       ||
                    s.queueVersion != lastStatus_.queueVersion ||
                    s.elapsed      != lastStatus_.elapsed      ||
                    s.repeat       != lastStatus_.repeat       ||
                    s.random       != lastStatus_.random);

    if (changed) {
        lastStatus_ = s;
        emit statusChanged(s);
    }

    // Adaptive polling: faster while playing for smooth UI updates,
    // slower when idle to reduce network chatter.
    if (s.state == MPD_STATE_PLAY)
        pollTimer_->setInterval(500);
    else
        pollTimer_->setInterval(2000);
}
