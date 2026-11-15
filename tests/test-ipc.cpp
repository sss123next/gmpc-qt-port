#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-ipc.cpp
 * @brief Single-instance IPC: server/client roundtrip via QLocalSocket.
 *
 * Tests IpcServer signal emission and sendIpcCommand() helper.
 * Socket path is redirected via XDG_RUNTIME_DIR → QTemporaryDir.
 */

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTest>

#include "tools/ipc.h"

// ── Global QCoreApplication (required by QLocalServer, QSignalSpy) ──────────
static int s_argc = 1;
static char s_arg0[] = "test-ipc";
static char *s_argv = s_arg0;
static QCoreApplication s_app(s_argc, &s_argv);

namespace {

/// Set XDG_RUNTIME_DIR to a temp dir for the duration of this scope.
struct ScopedRuntimeDir {
    QTemporaryDir dir;
    ScopedRuntimeDir() {
        REQUIRE(dir.isValid());
        qputenv("XDG_RUNTIME_DIR", dir.path().toUtf8());
    }
    ~ScopedRuntimeDir() { qunsetenv("XDG_RUNTIME_DIR"); }
    QString socketPath() const { return ipcSocketPath(); }
};

/// Connect a QLocalSocket to the IPC server, wait briefly, return success.
bool waitForConnect(QLocalSocket *sock, int ms = 2000)
{
    sock->connectToServer(ipcSocketPath());
    return sock->waitForConnected(ms);
}

/// Drain pending events so signals from the server arrive.
void pumpEvents()
{
    QTest::qWait(100);
}

} // namespace

// ── Server lifecycle ────────────────────────────────────────────────────────

TEST_CASE("IpcServer starts and accepts connections")
{
    ScopedRuntimeDir rt;
    IpcServer server;
    REQUIRE(server.start());

    QLocalSocket client;
    REQUIRE(waitForConnect(&client));
    CHECK(client.isOpen());
    CHECK(client.isWritable());

    client.disconnectFromServer();
    client.waitForDisconnected(1000);
}

TEST_CASE("IpcServer rejects duplicate instance")
{
    ScopedRuntimeDir rt;
    // Ensure a clean socket: remove any leftover from a previous test.
    QLocalServer::removeServer(ipcSocketPath());

    IpcServer s1;
    REQUIRE(s1.start());

    IpcServer s2;   // same socket path — listen() sees it's taken
    CHECK_FALSE(s2.start());
}

// ── Signal emission: leaf commands ──────────────────────────────────────────

TEST_CASE("show command emits showRequested")
{
    ScopedRuntimeDir rt;
    IpcServer server;
    REQUIRE(server.start());

    QSignalSpy spy(&server, &IpcServer::showRequested);

    QLocalSocket client;
    REQUIRE(waitForConnect(&client));
    client.write("show\n");
    client.waitForBytesWritten(500);
    pumpEvents();
    CHECK_EQ(spy.count(), 1);

    client.disconnectFromServer();
    client.waitForDisconnected(1000);
}

TEST_CASE("hide command emits hideRequested")
{
    ScopedRuntimeDir rt;
    IpcServer server;
    REQUIRE(server.start());

    QSignalSpy spy(&server, &IpcServer::hideRequested);

    QLocalSocket client;
    REQUIRE(waitForConnect(&client));
    client.write("hide\n");
    client.waitForBytesWritten(500);
    pumpEvents();
    CHECK_EQ(spy.count(), 1);

    client.disconnectFromServer();
    client.waitForDisconnected(1000);
}

TEST_CASE("toggle command emits toggleRequested")
{
    ScopedRuntimeDir rt;
    IpcServer server;
    REQUIRE(server.start());

    QSignalSpy spy(&server, &IpcServer::toggleRequested);

    QLocalSocket client;
    REQUIRE(waitForConnect(&client));
    client.write("toggle\n");
    client.waitForBytesWritten(500);
    pumpEvents();
    CHECK_EQ(spy.count(), 1);

    client.disconnectFromServer();
    client.waitForDisconnected(1000);
}

TEST_CASE("quit command emits quitRequested")
{
    ScopedRuntimeDir rt;
    IpcServer server;
    REQUIRE(server.start());

    QSignalSpy spy(&server, &IpcServer::quitRequested);

    QLocalSocket client;
    REQUIRE(waitForConnect(&client));
    client.write("quit\n");
    client.waitForBytesWritten(500);
    pumpEvents();
    CHECK_EQ(spy.count(), 1);

    client.disconnectFromServer();
    client.waitForDisconnected(1000);
}

TEST_CASE("preferences command emits preferencesRequested")
{
    ScopedRuntimeDir rt;
    IpcServer server;
    REQUIRE(server.start());

    QSignalSpy spy(&server, &IpcServer::preferencesRequested);

    QLocalSocket client;
    REQUIRE(waitForConnect(&client));
    client.write("preferences\n");
    client.waitForBytesWritten(500);
    pumpEvents();
    CHECK_EQ(spy.count(), 1);

    client.disconnectFromServer();
    client.waitForDisconnected(1000);
}

TEST_CASE("show notification command emits showNotificationRequested")
{
    ScopedRuntimeDir rt;
    IpcServer server;
    REQUIRE(server.start());

    QSignalSpy spy(&server, &IpcServer::showNotificationRequested);

    QLocalSocket client;
    REQUIRE(waitForConnect(&client));
    client.write("show notification\n");
    client.waitForBytesWritten(500);
    pumpEvents();
    CHECK_EQ(spy.count(), 1);

    client.disconnectFromServer();
    client.waitForDisconnected(1000);
}

// ── Commands with payload ───────────────────────────────────────────────────

TEST_CASE("stream: command emits streamRequested with payload")
{
    ScopedRuntimeDir rt;
    IpcServer server;
    REQUIRE(server.start());

    QSignalSpy spy(&server, &IpcServer::streamRequested);

    QLocalSocket client;
    REQUIRE(waitForConnect(&client));
    client.write("stream:http://stream.example/play.m3u\n");
    client.waitForBytesWritten(500);
    pumpEvents();
    REQUIRE_EQ(spy.count(), 1);
    CHECK_EQ(spy.takeFirst().at(0).toString(),
             QStringLiteral("http://stream.example/play.m3u"));

    client.disconnectFromServer();
    client.waitForDisconnected(1000);
}

TEST_CASE("easycommand: emits easyCommandReceived with payload")
{
    ScopedRuntimeDir rt;
    IpcServer server;
    REQUIRE(server.start());

    QSignalSpy spy(&server, &IpcServer::easyCommandReceived);

    QLocalSocket client;
    REQUIRE(waitForConnect(&client));
    client.write("easycommand:url http://radio.example\n");
    client.waitForBytesWritten(500);
    pumpEvents();
    REQUIRE_EQ(spy.count(), 1);
    CHECK_EQ(spy.takeFirst().at(0).toString(),
             QStringLiteral("url http://radio.example"));

    client.disconnectFromServer();
    client.waitForDisconnected(1000);
}

TEST_CASE("url: command emits urlRequested with payload")
{
    ScopedRuntimeDir rt;
    IpcServer server;
    REQUIRE(server.start());

    QSignalSpy spy(&server, &IpcServer::urlRequested);

    QLocalSocket client;
    REQUIRE(waitForConnect(&client));
    client.write("url:http://radio.example/stream.ogg\n");
    client.waitForBytesWritten(500);
    pumpEvents();
    REQUIRE_EQ(spy.count(), 1);
    CHECK_EQ(spy.takeFirst().at(0).toString(),
             QStringLiteral("http://radio.example/stream.ogg"));

    client.disconnectFromServer();
    client.waitForDisconnected(1000);
}

TEST_CASE("unknown command is silently ignored (no crash)")
{
    ScopedRuntimeDir rt;
    IpcServer server;
    REQUIRE(server.start());

    // Just ensure the server doesn't crash on garbage input.
    QLocalSocket client;
    REQUIRE(waitForConnect(&client));
    client.write("not-a-command\n");
    client.waitForBytesWritten(500);
    pumpEvents();
    // No crash — success.

    client.disconnectFromServer();
    client.waitForDisconnected(1000);
}

TEST_CASE("multiple commands in one connection are handled sequentially")
{
    ScopedRuntimeDir rt;
    IpcServer server;
    REQUIRE(server.start());

    QSignalSpy showSpy(&server, &IpcServer::showRequested);
    QSignalSpy hideSpy(&server, &IpcServer::hideRequested);

    QLocalSocket client;
    REQUIRE(waitForConnect(&client));
    client.write("show\nhide\nshow\n");
    client.waitForBytesWritten(500);
    pumpEvents();
    CHECK_EQ(showSpy.count(), 2);
    CHECK_EQ(hideSpy.count(), 1);

    client.disconnectFromServer();
    client.waitForDisconnected(1000);
}

TEST_CASE("multiple connections are handled independently")
{
    ScopedRuntimeDir rt;
    IpcServer server;
    REQUIRE(server.start());

    QSignalSpy spy(&server, &IpcServer::showRequested);

    QLocalSocket c1, c2;
    REQUIRE(waitForConnect(&c1));
    REQUIRE(waitForConnect(&c2));

    c1.write("show\n");
    c2.write("show\n");
    c1.waitForBytesWritten(500);
    c2.waitForBytesWritten(500);
    pumpEvents();

    CHECK_EQ(spy.count(), 2);

    c1.disconnectFromServer();
    c2.disconnectFromServer();
    c1.waitForDisconnected(1000);
    c2.waitForDisconnected(1000);
}

// ── sendIpcCommand helper ──────────────────────────────────────────────────

TEST_CASE("sendIpcCommand returns false when no server is running")
{
    ScopedRuntimeDir rt;
    QLocalServer::removeServer(ipcSocketPath());   // ensure clean state

    CHECK_FALSE(sendIpcCommand({.kind = IpcCommandKind::Show}));
    CHECK_FALSE(sendIpcCommand({}));                     // None kind
}

TEST_CASE("sendIpcCommand succeeds when a server is listening")
{
    ScopedRuntimeDir rt;
    IpcServer server;
    REQUIRE(server.start());

    QSignalSpy spy(&server, &IpcServer::showRequested);

    CHECK(sendIpcCommand({.kind = IpcCommandKind::Show}));

    pumpEvents();
    CHECK_EQ(spy.count(), 1);
}

TEST_CASE("sendIpcCommand delivers stream payload")
{
    ScopedRuntimeDir rt;
    IpcServer server;
    REQUIRE(server.start());

    QSignalSpy spy(&server, &IpcServer::streamRequested);

    CHECK(sendIpcCommand({.kind = IpcCommandKind::Stream,
                          .arg  = QStringLiteral("http://test/url")}));

    pumpEvents();
    REQUIRE_EQ(spy.count(), 1);
    CHECK_EQ(spy.takeFirst().at(0).toString(),
             QStringLiteral("http://test/url"));
}

TEST_CASE("sendIpcCommand delivers easycommand payload")
{
    ScopedRuntimeDir rt;
    IpcServer server;
    REQUIRE(server.start());

    QSignalSpy spy(&server, &IpcServer::easyCommandReceived);

    CHECK(sendIpcCommand({.kind = IpcCommandKind::Raw,
                          .arg  = QStringLiteral("preferences")}));

    pumpEvents();
    REQUIRE_EQ(spy.count(), 1);
    CHECK_EQ(spy.takeFirst().at(0).toString(), QStringLiteral("preferences"));
}

TEST_CASE("sendIpcCommand with None kind does nothing")
{
    ScopedRuntimeDir rt;
    IpcServer server;
    REQUIRE(server.start());

    QSignalSpy spy(&server, &IpcServer::showRequested);

    IpcCommand cmd;   // kind defaults to None
    CHECK_FALSE(sendIpcCommand(cmd));
    pumpEvents();
    CHECK_EQ(spy.count(), 0);
}

// ── ipcSocketPath helper ───────────────────────────────────────────────────

TEST_CASE("ipcSocketPath uses XDG_RUNTIME_DIR when set")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    qputenv("XDG_RUNTIME_DIR", dir.path().toUtf8());

    const QString path = ipcSocketPath();
    CHECK(path.startsWith(dir.path()));
    CHECK(path.endsWith(QStringLiteral("/gmpc-qt.sock")));

    qunsetenv("XDG_RUNTIME_DIR");
}
