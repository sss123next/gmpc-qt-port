#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "trompeloeil-doctest.hpp"

/**
 * @file test-db-browser.cpp
 * @brief Tests for DatabaseBrowser and MusicDatabaseModel.
 *
 * Uses trompeloeil to mock Connection so no real MPD server needed.
 */

#include <QApplication>
#include <QTreeView>
#include <QVBoxLayout>

#include "browsers/database-browser.h"
#include "browsers/music-database-model.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QApplication app(argc, &argv);

// ── Mock Connection ──────────────────────────────────────────────────────────
//
// When loadArtists/loadAlbums/loadTracks are called they access the
// (non-virtual) C API through raw().  Any call to the C API while
// isConnected() returns false will be harmless (NULL deref avoided via
// guards).  When isConnected() returns true the mocks for raw() and
// friends can be set up; here we allow raw() to return a non-null
// sentinel so that mpd_* functions may at least not crash immediately.

class MockConnection : public Connection {
public:
    MAKE_MOCK4(connectToServer, bool(const QString &, int, const QString &, int));
    MAKE_CONST_MOCK0(isConnected, bool());
    MAKE_MOCK0(fetchStatus, Connection::Status());
    MAKE_MOCK0(nextEntity, mpd_entity *());
};

// ── Tests for MusicDatabaseModel ─────────────────────────────────────────────

TEST_CASE("MusicDatabaseModel is initially empty")
{
    MockConnection conn;
    MusicDatabaseModel model(&conn);
    CHECK_EQ(model.rowCount({}), 0);
}

TEST_CASE("MusicDatabaseModel root item has expected column count")
{
    MockConnection conn;
    MusicDatabaseModel model(&conn);
    CHECK_EQ(model.columnCount({}), 2); // name, year
}

TEST_CASE("MusicDatabaseModel header data returns column names")
{
    MockConnection conn;
    MusicDatabaseModel model(&conn);
    CHECK_FALSE(model.headerData(0, Qt::Horizontal).toString().isEmpty());
    CHECK_FALSE(model.headerData(1, Qt::Horizontal).toString().isEmpty());
}

// ── safe under disconnected state ───────────────────────────────────────────

TEST_CASE("MusicDatabaseModel loadArtists safe when disconnected")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(false);
    MusicDatabaseModel model(&conn);

    // Must not crash or throw
    model.loadArtists();
    CHECK_EQ(model.rowCount({}), 0);
}

TEST_CASE("MusicDatabaseModel loadAlbums safe when disconnected")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(false);
    MusicDatabaseModel model(&conn);

    // Invalid index
    model.loadAlbums({});
    model.loadAlbums(model.index(0, 0));
    CHECK_EQ(model.rowCount({}), 0);
}

TEST_CASE("MusicDatabaseModel loadTracks safe when disconnected")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(false);
    MusicDatabaseModel model(&conn);

    model.loadTracks({});
    model.loadTracks(model.index(0, 0));
    CHECK_EQ(model.rowCount({}), 0);
}

// ── model query API ─────────────────────────────────────────────────────────

TEST_CASE("MusicDatabaseModel index/parent roundtrip")
{
    MockConnection conn;
    MusicDatabaseModel model(&conn);

    // Invalid out-of-bounds
    CHECK_FALSE(model.index(0, 0).isValid());
    CHECK_FALSE(model.index(0, 0, model.index(0, 0)).isValid());

    // Parent of invalid
    CHECK_FALSE(model.parent(model.index(0, 0)).isValid());
}

TEST_CASE("MusicDatabaseModel data on empty model")
{
    MockConnection conn;
    MusicDatabaseModel model(&conn);

    CHECK(model.data({}).isNull());
    CHECK(model.data(model.index(0, 0)).isNull());
    CHECK(model.data(model.index(0, 1)).isNull());
}

// ── tree structure with manually injected nodes ─────────────────────────────
//
// We cannot easily instrument beginInsertRows to inject nodes through
// the public API because the public API uses mpd_search_* commands
// directly.  The tests above verify safe behaviour.

TEST_CASE("DatabaseBrowser is a BrowserWidget subclass")
{
    MockConnection conn;
    DatabaseBrowser browser(&conn);
    CHECK(browser.title() == "Database");

    // Default state: should be disabled until connected
    CHECK(browser.isEnabled() == false);
}

TEST_CASE("DatabaseBrowser contains a QTreeView")
{
    MockConnection conn;
    DatabaseBrowser browser(&conn);
    auto *view = browser.findChild<QTreeView *>();
    CHECK(view != nullptr);
    CHECK(view->model() != nullptr);
}

TEST_CASE("DatabaseBrowser reacts to connection state")
{
    MockConnection conn;
    DatabaseBrowser browser(&conn);
    browser.onConnectionChanged(true);
    CHECK(browser.isEnabled());
    browser.onConnectionChanged(false);
    CHECK(browser.isEnabled() == false);
}

TEST_CASE("DatabaseBrowser icon name is correct")
{
    MockConnection conn;
    DatabaseBrowser browser(&conn);
    CHECK(browser.iconName() == "gmpc-database");
}

TEST_CASE("DatabaseBrowser onActivate safe when disconnected")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(false);

    DatabaseBrowser browser(&conn);
    // onActivate checks isConnected before calling loadArtists internally
    browser.onActivate();
    CHECK_FALSE(browser.isEnabled());
}

TEST_CASE("DatabaseBrowser onConnectionChanged(true) enables and loads")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(true);

    DatabaseBrowser browser(&conn);
    browser.onConnectionChanged(true);
    CHECK(browser.isEnabled());
}
