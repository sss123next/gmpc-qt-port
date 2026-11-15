/**
 * @file ipc.cpp
 * @brief Single-instance IPC implementation.
 *
 * Protocol: one-line, newline-terminated messages of the form
 *   command:payload
 *
 * Known commands:
 *   show
 *   hide
 *   toggle
 *   quit
 *   preferences
 *   show notification
 *   url:<address>
 *   stream:<url>
 *   bug information
 *   easycommand:<text>       (generic fallback)
 *
 * Socket location: $XDG_RUNTIME_DIR/gmpc-qt.sock
 * Falls back to /tmp/gmpc-qt-<uid>.sock when XDG_RUNTIME_DIR is unset.
 */

#include "tools/ipc.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QFileInfo>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QThread>
#include <QDir>

// ── Socket path ─────────────────────────────────────────────────────────────

QString ipcSocketPath()
{
    // Use XDG_RUNTIME_DIR when available (per-user, cleaned up on logout).
    QString dir = QString::fromUtf8(qgetenv("XDG_RUNTIME_DIR"));
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    return dir + QStringLiteral("/gmpc-qt.sock");
}

// ── Command parsing ───────────────────────────────────────────────────────

IpcCommand parseIpcCommandLine(int argc, char *argv[])
{
    // Minimal hand-rolled parser — QCommandLineParser in a secondary-instance
    // scenario would be overkill, but we already have QCoreApplication.
    // Re-use the application's own parser for consistency.
    QCommandLineParser p;
    p.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    p.addOption({"quit",              "Tell the running instance to quit."});
    p.addOption({"show",              "Tell the running instance to show the window."});
    p.addOption({"hide",              "Tell the running instance to hide the window."});
    p.addOption({"toggle",            "Toggle the running instance's window visibility."});
    p.addOption({"preferences",       "Open the preferences dialog of the running instance."});
    p.addOption({"show-notification", "Ask the running instance to show a tray notification."});
    p.addOption({"stream",            "Ask the running instance to load a stream URL.",
                 QStringLiteral("url")});       // expects a value
    p.addOption({"url",               "Ask the running instance to add a URL.",
                 QStringLiteral("address")});   // expects a value
    p.addOption({"bug-information",   "Show the bug information dialog."});
    p.addOption({"easycommand",       "Send an arbitrary easycommand text to the running instance.",
                 QStringLiteral("text")});      // expects a value

    // Allow "gmpc-qt <url>" as a shorthand for --stream <url>
    p.addPositionalArgument("url", "URL to play (shorthand for --stream).");

    p.process(*QCoreApplication::instance());

    IpcCommand cmd;

    if (p.isSet("quit"))                cmd.kind = IpcCommandKind::Quit;
    else if (p.isSet("show"))           cmd.kind = IpcCommandKind::Show;
    else if (p.isSet("hide"))           cmd.kind = IpcCommandKind::Hide;
    else if (p.isSet("toggle"))         cmd.kind = IpcCommandKind::Toggle;
    else if (p.isSet("preferences"))    cmd.kind = IpcCommandKind::Preferences;
    else if (p.isSet("show-notification")) cmd.kind = IpcCommandKind::ShowNotification;
    else if (p.isSet("bug-information"))   cmd.kind = IpcCommandKind::BugInformation;
    else if (p.isSet("stream")) {
        cmd.kind = IpcCommandKind::Stream;
        cmd.arg  = p.value("stream");
    }
    else if (p.isSet("url")) {
        cmd.kind = IpcCommandKind::Url;
        cmd.arg  = p.value("url");
    }
    else if (p.isSet("easycommand")) {
        cmd.kind = IpcCommandKind::Raw;
        cmd.arg  = p.value("easycommand");
    }
    else {
        // Positional argument → shorthand for --stream
        const QStringList pos = p.positionalArguments();
        if (!pos.isEmpty()) {
            cmd.kind = IpcCommandKind::Stream;
            cmd.arg  = pos.first();
        }
    }

    return cmd;
}

// ── Client-side: try to connect and send ────────────────────────────────────

static bool writeCommandToSocket(QLocalSocket *sock, const IpcCommand &cmd)
{
    if (!sock->isOpen() || !sock->isWritable())
        return false;

    QByteArray line;
    switch (cmd.kind) {
    case IpcCommandKind::Show:             line = "show\n";             break;
    case IpcCommandKind::Hide:             line = "hide\n";             break;
    case IpcCommandKind::Toggle:           line = "toggle\n";           break;
    case IpcCommandKind::Quit:             line = "quit\n";             break;
    case IpcCommandKind::Preferences:      line = "preferences\n";      break;
    case IpcCommandKind::ShowNotification: line = "show notification\n"; break;
    case IpcCommandKind::BugInformation:   line = "bug information\n";  break;
    case IpcCommandKind::Url:              line = "url:" + cmd.arg.toUtf8() + '\n';  break;
    case IpcCommandKind::Stream:           line = "stream:" + cmd.arg.toUtf8() + '\n'; break;
    case IpcCommandKind::Raw:              line = "easycommand:" + cmd.arg.toUtf8() + '\n'; break;
    case IpcCommandKind::None:             return false;
    }

    qint64 written = sock->write(line);
    if (written != line.size()) {
        qWarning("IPC: short write (%lld of %lld)", written, line.size());
        return false;
    }
    return sock->waitForBytesWritten(1000);
}

bool sendIpcCommand(const IpcCommand &cmd)
{
    if (cmd.kind == IpcCommandKind::None)
        return false;

    QLocalSocket sock;
    sock.connectToServer(ipcSocketPath());
    if (!sock.waitForConnected(2000)) {
        // No server listening — first instance.
        return false;
    }

    // Send the command.
    const bool ok = writeCommandToSocket(&sock, cmd);
    sock.disconnectFromServer();
    if (sock.state() != QLocalSocket::UnconnectedState)
        sock.waitForDisconnected(500);

    if (!ok) {
        qWarning("IPC: failed to send command, but a server was running.");
    }
    return ok;
}

// ── Server-side ─────────────────────────────────────────────────────────────

IpcServer::IpcServer(QObject *parent)
    : QObject(parent)
{
}

IpcServer::~IpcServer()
{
    if (server_) {
        server_->close();
        // Remove the socket file so a fresh instance can take over.
        QLocalServer::removeServer(ipcSocketPath());
    }
}

bool IpcServer::start()
{
    // NOTE: do NOT call QLocalServer::removeServer() here — that would let
    // a second instance steal the socket.  The caller (main.cpp) has already
    // verified no other instance is running and cleaned up stale sockets.
    server_ = new QLocalServer(this);
    connect(server_, &QLocalServer::newConnection,
            this, &IpcServer::onNewConnection);

    if (!server_->listen(ipcSocketPath())) {
        qWarning("IPC: cannot listen on %s — %s",
                 qPrintable(ipcSocketPath()),
                 qPrintable(server_->errorString()));
        delete server_;
        server_ = nullptr;
        return false;
    }

    qInfo().noquote() << "IPC listening on" << ipcSocketPath();
    return true;
}

void IpcServer::onNewConnection()
{
    while (server_->hasPendingConnections()) {
        QLocalSocket *client = server_->nextPendingConnection();
        connect(client, &QLocalSocket::readyRead, this, [this, client]() {
            while (client->canReadLine()) {
                const QByteArray line = client->readLine().trimmed();
                if (!line.isEmpty())
                    handleMessage(line);
            }
        });
        connect(client, &QLocalSocket::disconnected,
                client, &QLocalSocket::deleteLater);
        // If the client disappears without writing, still clean up.
        if (!client->isValid() && client->state() == QLocalSocket::ConnectedState)
            client->disconnectFromServer();
    }
}

void IpcServer::handleMessage(const QByteArray &line)
{
    // Split "command:payload"
    int colon = line.indexOf(':');
    QByteArray command;
    QByteArray payload;
    if (colon >= 0) {
        command = line.left(colon);
        payload = line.mid(colon + 1);
    } else {
        command = line;
    }

    qDebug("IPC: received <%s>", line.constData());

    // Dispatch.
    if (command == "show")                      emit showRequested();
    else if (command == "hide")                 emit hideRequested();
    else if (command == "toggle")               emit toggleRequested();
    else if (command == "quit")                 emit quitRequested();
    else if (command == "preferences")          emit preferencesRequested();
    else if (command == "show notification")    emit showNotificationRequested();
    else if (command == "bug information")      emit easyCommandReceived(QString::fromUtf8(line));
    else if (command == "url")                  emit urlRequested(QString::fromUtf8(payload));
    else if (command == "stream")               emit streamRequested(QString::fromUtf8(payload));
    else if (command == "easycommand")
        emit easyCommandReceived(QString::fromUtf8(payload));
    else
        qWarning("IPC: unknown command <%s>", command.constData());
}
