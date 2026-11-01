#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-mpd-error.cpp
 * @brief Классификация ошибок libmpdclient и разбор имён путей MPD.
 */

#include <QApplication>
#include <QSignalSpy>

#include <mpd/client.h>

#include "browsers/file-system-model.h"
#include "core/connection.h"
#include "core/mpd-error.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QApplication app(argc, &argv);

TEST_CASE("classifyMpdError: no error means no action")
{
    CHECK(classifyMpdError(MPD_ERROR_SUCCESS) == MpdErrorAction::None);
}

TEST_CASE("classifyMpdError: server-side rejection keeps the connection")
{
    // ACK от сервера и неверный аргумент — команда не выполнена, соединение живое.
    CHECK(classifyMpdError(MPD_ERROR_SERVER) == MpdErrorAction::KeepConnection);
    CHECK(classifyMpdError(MPD_ERROR_ARGUMENT) == MpdErrorAction::KeepConnection);
}

TEST_CASE("classifyMpdError: transport failures need a new connection")
{
    CHECK(classifyMpdError(MPD_ERROR_OOM) == MpdErrorAction::Reconnect);
    CHECK(classifyMpdError(MPD_ERROR_TIMEOUT) == MpdErrorAction::Reconnect);
    CHECK(classifyMpdError(MPD_ERROR_SYSTEM) == MpdErrorAction::Reconnect);
    CHECK(classifyMpdError(MPD_ERROR_RESOLVER) == MpdErrorAction::Reconnect);
    CHECK(classifyMpdError(MPD_ERROR_MALFORMED) == MpdErrorAction::Reconnect);
    CHECK(classifyMpdError(MPD_ERROR_CLOSED) == MpdErrorAction::Reconnect);
}

TEST_CASE("classifyMpdError: MPD_ERROR_STATE is reconnect, not keep")
{
    // Замеры на живом MPD 2.24 (build/rf_probe.c): после «Cannot send a new
    // command while receiving another response» соединение не восстанавливается
    // ни mpd_response_finish(), ни mpd_connection_clear_error(). Документация
    // заголовка обещает обратное — тест фиксирует эмпирический результат.
    CHECK(classifyMpdError(MPD_ERROR_STATE) == MpdErrorAction::Reconnect);
}

TEST_CASE("classifyMpdError: unknown codes are treated as fatal")
{
    CHECK(classifyMpdError(-1) == MpdErrorAction::Reconnect);
    CHECK(classifyMpdError(42) == MpdErrorAction::Reconnect);
}

TEST_CASE("mpdBasename: last component of an MPD-relative path")
{
    CHECK_EQ(mpdBasename(QStringLiteral("humor/karcev")), QStringLiteral("karcev"));
    CHECK_EQ(mpdBasename(QStringLiteral("a/b/c/")), QStringLiteral("c"));
    CHECK_EQ(mpdBasename(QStringLiteral("song.opus")), QStringLiteral("song.opus"));
    CHECK_EQ(mpdBasename(QStringLiteral("a//b///")), QStringLiteral("b"));
}

TEST_CASE("mpdBasename: degenerate paths give an empty name")
{
    CHECK(mpdBasename({}).isEmpty());
    CHECK(mpdBasename(QStringLiteral("/")).isEmpty());
    CHECK(mpdBasename(QStringLiteral("///")).isEmpty());
}

TEST_CASE("ResponseScope on a disconnected connection is inert")
{
    // Деструктор вызывает consumeError(): на отсутствующем соединении он обязан
    // ничего не сообщать и не ронять приложение (модели вызывают его всегда).
    Connection conn;
    QSignalSpy failed(&conn, &Connection::commandFailed);
    {
        Connection::ResponseScope scope(conn, "lsinfo");
    }
    CHECK_EQ(failed.count(), 0);
    CHECK_FALSE(conn.isConnected());
}
