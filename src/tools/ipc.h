#pragma once

/**
 * @file ipc.h
 * @brief Single-instance IPC via QLocalServer/QLocalSocket.
 *
 * Replaces the original GMPC's libunique-based IPC (Tools/ipc.c).
 * Provides:
 *   - IpcServer — listens on a Unix socket for commands from secondary
 *     instances (show, hide, toggle, quit, preferences, url, stream).
 *   - sendIpcCommand() — try to connect to a running instance and
 *     deliver a command; returns true if another instance served it.
 *   - ipcSocketPath() — the well-known socket path for this user.
 */

#include <QObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QString>

// ── Helpers ─────────────────────────────────────────────────────────────────

/// Return the well-known IPC socket path for this user.
QString ipcSocketPath();

// ── Command ─────────────────────────────────────────────────────────────────

/// High-level IPC command parsed from argv / easycommand text.
enum class IpcCommandKind : uint8_t {
    None,
    Show,          ///< Raise window.
    Hide,          ///< Hide window (minimise).
    Toggle,        ///< Toggle show/hide.
    Quit,          ///< Terminate the running instance.
    Preferences,   ///< Open preferences dialog.
    ShowNotification, ///< Show a tray notification (if tray enabled).
    Url,           ///< Load a URL (playlist/stream) via MPD's "load".
    Stream,        ///< Same as Url but from --stream / stream: IPC command.
    BugInformation,///< Show bug-info window (stub in Qt version).
    Raw,           ///< Arbitrary easycommand text passed through.
};

/// A parsed IPC command, ready to send or dispatch.
struct IpcCommand {
    IpcCommandKind kind = IpcCommandKind::None;
    QString arg;   ///< Payload for Url/Stream/Raw commands.
};

/// Parse argc/argv into an IpcCommand to be sent to the running instance.
IpcCommand parseIpcCommandLine(int argc, char *argv[]);

// ── Client-side (send to running instance) ──────────────────────────────────

/**
 * @brief Try to connect to a running instance and deliver @p cmd.
 * @return true if a running instance accepted the command.
 *
 * The caller should exit immediately after this returns true.
 */
bool sendIpcCommand(const IpcCommand &cmd);

// ── Server-side (listen for secondary instances) ────────────────────────────

/**
 * @brief IPC server that owns the well-known Unix socket.
 *
 * Create one in main() when no other instance is running, then
 * connect its signals to the MainWindow's slots.
 */
class IpcServer : public QObject {
    Q_OBJECT
public:
    explicit IpcServer(QObject *parent = nullptr);
    ~IpcServer() override;

    /// Start listening. Returns false if the socket is already taken.
    bool start();

signals:
    /// Emitted when another instance asks the window to show/raise.
    void showRequested();
    void hideRequested();
    void toggleRequested();
    void quitRequested();
    void preferencesRequested();
    void showNotificationRequested();
    /// A URL to be loaded via MpdCommands::addUrl().
    void urlRequested(const QString &url);
    /// Same as urlRequested but from the legacy stream: command.
    void streamRequested(const QString &url);
    /// Fallback: any unrecognised or raw easycommand text.
    void easyCommandReceived(const QString &text);

private slots:
    void onNewConnection();

private:
    void handleMessage(const QByteArray &line);

    QLocalServer *server_ = nullptr;
};
