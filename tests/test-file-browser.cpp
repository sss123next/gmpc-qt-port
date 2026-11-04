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

TEST_CASE("FileSystemModel has three columns")
{
    MockConnection conn;
    FileSystemModel model(&conn);
    CHECK_EQ(model.columnCount({}), 3);
}

TEST_CASE("FileSystemModel header returns names for all columns")
{
    MockConnection conn;
    FileSystemModel model(&conn);
    auto h0 = model.headerData(0, Qt::Horizontal);
    CHECK_FALSE(h0.toString().isEmpty());
    auto h1 = model.headerData(1, Qt::Horizontal);
    CHECK_FALSE(h1.toString().isEmpty());
    auto h2 = model.headerData(2, Qt::Horizontal);
    CHECK_FALSE(h2.toString().isEmpty());
    // Out-of-range column returns null
    CHECK(model.headerData(3, Qt::Horizontal).isNull());
}

TEST_CASE("FileSystemModel column 0 header is Name")
{
    MockConnection conn;
    FileSystemModel model(&conn);
    CHECK_EQ(model.headerData(0, Qt::Horizontal).toString(), "Name");
}

TEST_CASE("FileSystemModel column 1 header is Type")
{
    MockConnection conn;
    FileSystemModel model(&conn);
    CHECK_EQ(model.headerData(1, Qt::Horizontal).toString(), "Type");
}

TEST_CASE("FileSystemModel column 2 header is Duration")
{
    MockConnection conn;
    FileSystemModel model(&conn);
    CHECK_EQ(model.headerData(2, Qt::Horizontal).toString(), "Duration");
}

TEST_CASE("FileSystemModel UriRole returns empty for invalid index")
{
    MockConnection conn;
    FileSystemModel model(&conn);
    QVariant uri = model.data(QModelIndex(), FileSystemModel::UriRole);
    CHECK(uri.isNull());
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
    // A browser no longer disables itself: BrowserStack gates it on connection.
    CHECK(browser.isEnabled());
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

// ── Двойной клик: тип строки и навигация ────────────────────────────────────

TEST_CASE("FileSystemModel TypeRole is empty for an invalid index")
{
    MockConnection conn;
    FileSystemModel model(&conn);
    // Пустой путь и пустой тип — активация по такому индексу ничего не делает.
    CHECK(model.data({}, FileSystemModel::TypeRole).isNull());
}

TEST_CASE("FileBrowser exposes both panes and ignores invalid activation")
{
    MockConnection conn;
    FileBrowser browser(&conn);
    REQUIRE(browser.treeView() != nullptr);
    REQUIRE(browser.contentView() != nullptr);
    // Двойной клик вешается на правую панель, а навигация меняет выделение в левой.
    CHECK(browser.treeView() != browser.contentView());

    browser.enterDirectory({});   // невалидная строка правой панели — no-op
    CHECK_EQ(browser.contentView()->model()->rowCount({}), 0);
}
