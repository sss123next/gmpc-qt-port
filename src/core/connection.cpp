/**
 * @file connection.cpp
 * @brief Implementation of the MPD Connection wrapper.
 */

#include "connection.h"
#include "core/mpd-error.h"
#include "tools/log.h"

#include <cstring>
#include <algorithm>

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

    host_ = host;
    port_ = port;
    password_ = password;
    timeoutMs_ = timeoutMs;

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

    LOG_INFO("Connected to MPD at %s (port %d)",
             host.isEmpty() ? "<from environment>"
                            : qPrintable(host), port);
    connected_ = true;
    if (!autoReconnecting_)
        reconnectDelayMs_ = 0;
    pollTimer_->start();
    emit connected();
    return true;
}

void Connection::disconnect()
{
    pollTimer_->stop();
    if (reconnectTimer_)
        reconnectTimer_->stop();
    // Задержку сбрасывает только успешное подключение: при автопереподключении
    // disconnect() вызывается из connectToServer(), и обнуление превратило бы
    // экспоненциальную задержку в вечный повтор раз в секунду.
    if (!autoReconnecting_)
        reconnectDelayMs_ = 0;

    if (conn_) {
        mpd_connection_free(conn_);
        conn_ = nullptr;
    }
    const bool wasConnected = connected_;
    lastStatus_ = Status{};
    connected_ = false;
    if (wasConnected)
        emit disconnected();
}

bool Connection::isConnected() const
{
    return conn_ != nullptr && connected_;
}

QString Connection::consumeError(const char *context)
{
    if (!conn_)
        return {};

    // Всегда завершаем ответ, а не только при ошибке: недочитанный ответ —
    // единственная причина безвозвратного MPD_ERROR_STATE (замеры rf_probe.c).
    // На уже прочитанном или не начатом ответе mpd_response_finish() — no-op.
    mpd_response_finish(conn_);

    const enum mpd_error code = mpd_connection_get_error(conn_);
    if (code == MPD_ERROR_SUCCESS)
        return {};

    // get_server_error() валиден только для MPD_ERROR_SERVER — вне его он
    // утверждает ассертом.
    QString message = QString::fromUtf8(mpd_connection_get_error_message(conn_));
    if (code == MPD_ERROR_SERVER) {
        message = tr("MPD error %1: %2")
                      .arg(static_cast<int>(mpd_connection_get_server_error(conn_)))
                      .arg(message);
    }

    const MpdErrorAction action = classifyMpdError(static_cast<int>(code));
    mpd_connection_clear_error(conn_);

    LOG_WARN("MPD command failed (%s): %s",
             context ? context : "mpd", qPrintable(message));
    emit commandFailed(context ? tr("%1: %2").arg(QString::fromUtf8(context), message)
                              : message);

    // Осталась ошибка => недочитанный ответ: штатными средствами не лечится.
    if (action == MpdErrorAction::Reconnect
        || mpd_connection_get_error(conn_) != MPD_ERROR_SUCCESS) {
        dropAndReconnect();
    }

    return message;
}

Connection::Status Connection::fetchStatus()
{
    Status s;
    if (!conn_)
        return s;

    mpd_status *status = mpd_run_status(conn_);
    if (!status) {
        consumeError("status");
        return s;
    }
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
    if (!conn_ || !connected_)
        return result;

    // Деструктор завершает ответ и снимает error-state — в том числе на ветках
    // с ранним return, где раньше соединение и оставалось клинить.
    ResponseScope scope(*this, "command");

    if (!mpd_send_command(conn_, cmd.toUtf8().constData(), nullptr))
        return {};

    // Read all response pairs
    while (auto *pair = mpd_recv_pair(conn_)) {
        result.append({ QString::fromUtf8(pair->name),
                        QString::fromUtf8(pair->value) });
        mpd_return_pair(conn_, pair);
    }

    if (!mpd_response_finish(conn_))
        return {};

    return result;
}

void Connection::pollStatus()
{
    // Соединение уже сброшено consumeError(); повторное подключение ведёт
    // reconnectTimer_.
    if (!isConnected())
        return;

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

    // Адаптивный шаг: при воспроизведении тики нужны прогресс-бару (idle о
    // прошедших секундах не сообщает), в простое опрос — подстраховка, и пока
    // события приходят из слушателя, достаточно редкого тика.
    if (s.state == MPD_STATE_PLAY)
        pollTimer_->setInterval(kDefaultPollMs);
    else
        pollTimer_->setInterval(idleWatched_ ? kIdleWatchedPollMs : kStoppedPollMs);
}

void Connection::refreshNow()
{
    // Таймер не трогаем: pollStatus() сам выставит интервал по состоянию, а
    // остановка превратила бы разовый опрос в прекращение опроса вообще.
    if (!isConnected())
        return;

    pollStatus();
}

void Connection::setIdleWatched(bool watched)
{
    if (idleWatched_ == watched)
        return;

    idleWatched_ = watched;

    // При потере событий интервал возвращаем сразу: ждать следующий редкий тик,
    // чтобы начать опрашивать часто, — ровно тот промах, которого избегаем.
    if (!watched && isConnected()) {
        pollTimer_->setInterval(lastStatus_.state == MPD_STATE_PLAY ? kDefaultPollMs
                                                                   : kStoppedPollMs);
        refreshNow();   // всё, что случилось за время молчания слушателя
    }
}

void Connection::dropAndReconnect()
{
    if (conn_) {
        mpd_connection_free(conn_);
        conn_ = nullptr;
    }
    const bool wasConnected = connected_;
    lastStatus_ = Status{};
    connected_ = false;
    pollTimer_->stop();

    if (wasConnected)
        emit disconnected();

    scheduleReconnect();
}

void Connection::scheduleReconnect()
{
    if (!reconnectTimer_) {
        reconnectTimer_ = new QTimer(this);
        reconnectTimer_->setSingleShot(true);
        connect(reconnectTimer_, &QTimer::timeout,
                this, &Connection::tryReconnect);
    }

    reconnectDelayMs_ = reconnectDelayMs_ <= 0
        ? kReconnectBaseMs
        : std::min(reconnectDelayMs_ * 2, kReconnectMaxMs);

    LOG_INFO("Reconnecting to MPD in %d ms", reconnectDelayMs_);
    reconnectTimer_->start(reconnectDelayMs_);
}

void Connection::tryReconnect()
{
    autoReconnecting_ = true;
    const bool ok = connectToServer(host_, port_, password_, timeoutMs_);
    autoReconnecting_ = false;

    if (ok) {
        reconnectDelayMs_ = 0;
        LOG_INFO("Reconnected to MPD");
        return;
    }

    scheduleReconnect();
}
