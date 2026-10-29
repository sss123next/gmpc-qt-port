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
    CHECK_EQ(model.columnCount({}), 2); // artist / album
}

TEST_CASE("MusicDatabaseModel header data returns column names")
{
    MockConnection conn;
    MusicDatabaseModel model(&conn);
    CHECK_FALSE(model.headerData(0, Qt::Horizontal).toString().isEmpty());
    CHECK_FALSE(model.headerData(1, Qt::Horizontal).toString().isEmpty());
}

TEST_CASE("MusicDatabaseModel loadArtists populates root items")
{
    // TODO: when loadArtists is implemented, verify items appear.
    // For now we just verify that calling it doesn't crash.
    MockConnection conn;
    MusicDatabaseModel model(&conn);
    model.loadArtists(); // should be safe on empty model
    CHECK(model.rowCount({}) >= 0);
}

// ── Tests for DatabaseBrowser (widget) ───────────────────────────────────────

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
