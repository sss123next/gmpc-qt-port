#pragma once

/**
 * @file connection.h
 * @brief Thin C++ wrapper around libmpdclient's mpd_connection.
 *
 * Connection owns a ::mpd_connection handle, provides a polling timer
 * (every 500 ms) that emits statusChanged(), and exposes the raw
 * handle for advanced use via raw().
 *
 * Thread-safety:  all methods must be called from the Qt main (GUI)
 * thread, just like any other QObject.
 */

#include <QObject>
#include <QString>
#include <QTimer>
#include <mpd/client.h>

/// High-level wrapper around libmpdclient.
class Connection : public QObject {
    Q_OBJECT
public:
    /// Интервал опроса при воспроизведении и до первого полученного статуса (мс).
    static constexpr int kDefaultPollMs = 500;

    /** @brief Construct an unconnected Connection.
     *  @param parent  Qt parent object.
     */
    explicit Connection(QObject *parent = nullptr);
    ~Connection() override;

    // -- connection lifecycle ------------------------------------------------

    /** @brief Open a connection to an MPD server.
     *
     *  libmpdclient auto-detects Unix-domain sockets when \a host
     *  starts with '/' or '@' (an abstract socket).  Otherwise TCP is used.
     *
     *  Passing an empty \a host, a non-positive \a port or a non-positive
     *  \a timeoutMs leaves that setting unspecified, and libmpdclient then
     *  consults MPD_HOST / MPD_PORT / MPD_TIMEOUT itself (host ultimately
     *  defaulting to localhost).  Callers that must honour MPD_PASSWORD —
     *  which the library ignores — resolve it first via MpdResolvedSettings.
     *
     *  @param host      Hostname / IP or Unix socket path.
     *  @param port      TCP port (ignored for Unix sockets; 0 = default).
     *  @param password  Optional password (empty = no auth).
     *  @param timeoutMs Socket timeout in milliseconds.
     *  @return true on success.
     *
     *  On success the polling timer is started and signal connected() fires.
     *  On failure errorOccurred() fires and false is returned.
     */
    bool connectToServer(const QString &host = QString(),
                         int port = 0,
                         const QString &password = QString(),
                         int timeoutMs = 0);

    /// Close the MPD connection and stop the polling timer.
    void disconnect();

    /** @returns true, если соединение открыто и не сброшено в error-state.
     *
     *  Намеренно не зависит от mpd_connection_get_error(): одиночная неудачная
     *  команда — это ошибка команды, а не потеря соединения (см. consumeError()).
     *
     *  Виртуальна ради тестов: виджеты держат Connection*, и без полиморфизма
     *  подменённый в юните мок молча обходился бы — вызов уходил бы в настоящую
     *  реализацию (у неё нет mpd_connection, поэтому всегда false).
     */
    virtual bool isConnected() const;

    /** @brief Есть ли живой handle libmpdclient.
     *
     *  Отдельно от isConnected(): в юните флаг «подключено» выставляет мок, а
     *  mpd_connection у него нет вовсе — любая функция библиотеки падает на NULL
     *  (assert send_check). Поэтому чтение с сервера требует handle, а отправка
     *  команды через подменённый MpdCommands — только флага.
     */
    bool hasHandle() const { return conn_ != nullptr; }

    // -- параметры последнего подключения ------------------------------------

    /** @name Настройки, с которыми было открыто соединение.
     *
     *  libmpdclient не даёт второе соединение из первого, а поиску нужен свой
     *  handle (одно соединение обслуживает один поток). Значения — ровно те, что
     *  передавались в connectToServer(); разрешение MPD_HOST/MPD_PASSWORD до вызова
     *  остаётся за вызывающим (см. MpdResolvedSettings), поэтому host пустым здесь
     *  не бывает после успешного подключения.
     */
    ///@{
    const QString &host() const { return host_; }
    int port() const { return port_; }
    const QString &password() const { return password_; }
    int timeoutMs() const { return timeoutMs_; }
    ///@}

    // -- ошибки команд -------------------------------------------------------

    /** @brief Завершает текущий ответ MPD и выходит из error-state.
     *  @param context Имя команды для сообщения и лога (может быть nullptr).
     *  @returns текст ошибки или пустую строку, если ошибки не было.
     *
     *  Обязателен после любой неудачной mpd_send_… / mpd_run_…: libmpdclient
     *  оставляет соединение в error-state, и все последующие команды падают с
     *  «Cannot send a new command while receiving another response». Командная
     *  ошибка (ACK сервера) испускает commandFailed() и соединение сохраняет;
     *  фатальная — закрывает его и запускает переподключение с экспоненциальной
     *  задержкой.
     */
    QString consumeError(const char *context = nullptr);

    /** Завершает ответ MPD и снимает error-state в деструкторе, поэтому работает
     *  и при выходе из блока по return/`goto`. mpd_response_finish() безопасен на
     *  уже прочитанном или не начатом ответе (замерено: возвращает true, не блокирует). */
    class ResponseScope {
    public:
        ResponseScope(Connection &conn, const char *context)
            : conn_(conn), context_(context) {}
        ~ResponseScope() { conn_.consumeError(context_); }

        ResponseScope(const ResponseScope &) = delete;
        ResponseScope &operator=(const ResponseScope &) = delete;

    private:
        Connection &conn_;
        const char *context_;
    };

    // -- status --------------------------------------------------------------

    /** Snapshot of MPD status as returned by mpd_run_status(). */
    struct Status {
        int state = MPD_STATE_UNKNOWN; ///< MPD_STATE_STOP/PLAY/PAUSE/UNKNOWN
        int volume = -1;               ///< 0–100 or -1 if unknown
        int songId = -1;               ///< currently playing song id
        int songPos = -1;              ///< position in queue
        int queueLength = 0;
        int queueVersion = 0;
        float elapsed = 0;             ///< seconds elapsed
        float totalDuration = 0;       ///< total track duration
        int bitRate = 0;               ///< kbit/s (0 if unknown)
        unsigned sampleRate = 0;       ///< Hz (0 = unknown)
        uint8_t bits = 0;              ///< bit depth or MPD_SAMPLE_FORMAT_*
        uint8_t channels = 0;          ///< 0 = unknown
        bool repeat = false;
        bool random = false;
        bool single = false;
        bool consume = false;
        int crossfade = 0;             ///< crossfade seconds
    };

    /** @brief Fetch a fresh Status snapshot from the server.
     *  @returns Status (default-initialised on error).
     */
    Status fetchStatus();

    /** @brief Iterate over received mpd_entity objects.
     *  @returns next entity or nullptr when iteration is done.
     *
     *  Must be called after mpd_send_list_queue_meta() or similar.
     *  Caller must mpd_entity_free() each returned entity.
     */
    mpd_entity *nextEntity();

    /** @brief Send a raw MPD command and parse the key-value response.
     *
     *  Reads all (key, value) pairs returned by the server into a list.
     *  On error the list is empty; use raw() to check connection state.
     *
     *  @param cmd  Command string (e.g. "status").
     *  @returns List of key-value pairs in server order.
     */
    QList<QPair<QString, QString>> rawCommand(const QString &cmd);

    /** @brief Access the raw libmpdclient handle.
     *  @returns pointer to ::mpd_connection or nullptr.
     *
     *  Used internally by MpdCommands to pass the handle to
     *  mpd_run_*() functions.
     */
    mpd_connection *raw() const { return conn_; }

    // -- опрос состояния -----------------------------------------------------

    /** @brief Немедленно опросить статус, не дожидаясь следующего тика таймера.
     *
     *  Нужно слушателю событий (IdleWatcher): когда MPD сообщил `player` или
     *  `mixer`, ждать очередного тика смысла нет — статус надо забрать сейчас.
     *  Вне соединения это no-op; statusChanged() возникнет сам, если поля
     *  действительно изменились.
     */
    void refreshNow();

    /** @brief Сообщить, что события приходят из idle, а не только из таймера.
     *
     *  Пока слушатель жив, опрос — подстраховка на случай потерянного события, и
     *  в простое достаточно одного тика раз в #kIdleWatchedPollMs. Как только
     *  события отваливаются, интервал возвращается к #kStoppedPollMs: без idle
     *  редкий тик означал бы молчаливое расхождение UI с сервером.
     */
    void setIdleWatched(bool watched);

signals:
    /// Emitted after a successful connectToServer().
    void connected();

    /// Emitted after disconnect() or when the polling timer detects a drop.
    void disconnected();

    /// Emitted on every polling tick when any status field changed.
    void statusChanged(const Connection::Status &status);

    /// Emitted on connection errors.
    void errorOccurred(const QString &message);

    /** @brief MPD отклонил команду (ACK сервера или некорректные аргументы).
     *
     *  Соединение при этом остаётся подключённым — экран не надо переводить в
     *  «офлайн», достаточно показать сообщение.
     */
    void commandFailed(const QString &message);

private slots:
    /// Timer callback — fetches status and emits statusChanged if different.
    void pollStatus();

    /// Повторная попытка подключиться после потери соединения.
    void tryReconnect();

private:
    /** Закрывает соединение, испускает disconnected() и планирует попытку заново. */
    void dropAndReconnect();

    /// Ставит single-shot таймер переподключения с удвоенной задержкой.
    void scheduleReconnect();

    mpd_connection *conn_ = nullptr;
    QTimer *pollTimer_ = nullptr;
    Status lastStatus_;

    // Настройки последнего connectToServer() — для автопереподключения.
    QString host_;
    int port_ = 0;
    QString password_;
    int timeoutMs_ = 0;

    bool connected_ = false;       ///< соединение установлено успешно
    bool autoReconnecting_ = false;///< идём на reconnect (не сбрасывать задержку)
    bool idleWatched_ = false;     ///< события приходят из IdleWatcher
    QTimer *reconnectTimer_ = nullptr;
    int reconnectDelayMs_ = 0;     ///< 0 = переподключение не запланировано

    static constexpr int kReconnectBaseMs = 1000;
    static constexpr int kReconnectMaxMs = 30000;

    /// Интервалы опроса: kDefaultPollMs — при воспроизведении (прогресс-бару
    /// нужны частые тики, idle о прошедших секундах не сообщает).
    static constexpr int kStoppedPollMs = 2000;

    /// Опрос в простое, пока события идут из idle: только подстраховка.
    static constexpr int kIdleWatchedPollMs = 10000;
};

Q_DECLARE_METATYPE(Connection::Status)

