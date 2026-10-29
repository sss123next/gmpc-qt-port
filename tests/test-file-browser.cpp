#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "trompeloeil-doctest.hpp"

/**
 * @file test-file-browser.cpp
 * @brief Tests for FileBrowser and FileSystemModel.
 *
 * Uses trompeloeil to mock Connection so no real MPD server needed.
 */

#include <QApplication>
#include <QTreeView>

#include "browsers/file-browser.h"
#include "browsers/file-system-model.h"

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

// ── Tests for FileSystemModel ─────────────────────────────────────────────────

TEST_CASE("FileSystemModel is initially empty")
{
    MockConnection conn;
    FileSystemModel model(&conn);
    CHECK_EQ(model.rowCount({}), 0);
    // Root has no children, canFetchMore returns true (not loaded yet)
    CHECK_EQ(model.canFetchMore({}), true);
}

TEST_CASE("FileSystemModel has single column")
{
    MockConnection conn;
    FileSystemModel model(&conn);
    CHECK_EQ(model.columnCount({}), 1);
}

TEST_CASE("FileSystemModel header returns name")
{
    MockConnection conn;
    FileSystemModel model(&conn);
    auto h = model.headerData(0, Qt::Horizontal);
    CHECK_FALSE(h.toString().isEmpty());
}

TEST_CASE("FileSystemModel default state is safe")
{
    MockConnection conn;
    FileSystemModel model(&conn);
    // Out-of-bounds access should be safe
    CHECK_FALSE(model.index(0, 0).isValid());
    CHECK_FALSE(model.index(0, 0, model.index(0, 0)).isValid());
    CHECK_FALSE(model.parent(model.index(0, 0)).isValid());
    // data on invalid index is quiet
    CHECK(model.data({}).isNull());
}

// ── Tests for FileBrowser (widget) ───────────────────────────────────────────

TEST_CASE("FileBrowser is a BrowserWidget subclass")
{
    MockConnection conn;
    FileBrowser browser(&conn);
    CHECK(browser.title() == "Files");
    // Default state: disabled until connected
    CHECK_FALSE(browser.isEnabled());
}

TEST_CASE("FileBrowser contains a QTreeView")
{
    MockConnection conn;
    FileBrowser browser(&conn);
    auto *view = browser.findChild<QTreeView *>();
    CHECK(view != nullptr);
    CHECK(view->model() != nullptr);
}

TEST_CASE("FileBrowser reacts to connection state")
{
    MockConnection conn;
    FileBrowser browser(&conn);
    browser.onConnectionChanged(true);
    CHECK(browser.isEnabled());
    browser.onConnectionChanged(false);
    CHECK_FALSE(browser.isEnabled());
}

TEST_CASE("FileBrowser icon name is correct")
{
    MockConnection conn;
    FileBrowser browser(&conn);
    CHECK(browser.iconName() == "folder");
}
