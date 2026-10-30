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
    /// Default polling interval (ms).
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

    /// @returns true if conn_ is non-null and has no error.
    bool isConnected() const;

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

signals:
    /// Emitted after a successful connectToServer().
    void connected();

    /// Emitted after disconnect() or when the polling timer detects a drop.
    void disconnected();

    /// Emitted on every polling tick when any status field changed.
    void statusChanged(const Connection::Status &status);

    /// Emitted on connection errors.
    void errorOccurred(const QString &message);

private slots:
    /// Timer callback — fetches status and emits statusChanged if different.
    void pollStatus();

private:
    mpd_connection *conn_ = nullptr;
    QTimer *pollTimer_ = nullptr;
    Status lastStatus_;
};

Q_DECLARE_METATYPE(Connection::Status)

