#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "trompeloeil-doctest.hpp"

/**
 * @file test-playlist-editor.cpp
 * @brief Tests for PlaylistEditor.
 *
 * Uses trompeloeil to mock Connection (no real MPD server needed).
 */

#include <QApplication>
#include <QListWidget>
#include <QPushButton>

#include "browsers/playlist-editor.h"

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

// ── Tests ────────────────────────────────────────────────────────────────────

TEST_CASE("PlaylistEditor is a BrowserWidget subclass")
{
    MockConnection conn;
    PlaylistEditor editor(&conn);
    CHECK(editor.title() == "Playlists");

    // BrowserStack decides enabled state from the connection, not the widget.
    CHECK(editor.isEnabled());
}

TEST_CASE("PlaylistEditor contains two QListWidgets")
{
    MockConnection conn;
    PlaylistEditor editor(&conn);
    auto lists = editor.findChildren<QListWidget *>();
    CHECK_EQ(lists.size(), 2);
}

TEST_CASE("PlaylistEditor contains four QPushButtons")
{
    MockConnection conn;
    PlaylistEditor editor(&conn);
    auto btns = editor.findChildren<QPushButton *>();
    // New, Rename, Delete, Load
    CHECK_EQ(btns.size(), 4);
}

TEST_CASE("PlaylistEditor reacts to connection state")
{
    MockConnection conn;
    PlaylistEditor editor(&conn);
    editor.onConnectionChanged(true);
    CHECK(editor.isEnabled());
    editor.onConnectionChanged(false);
    CHECK_FALSE(editor.isEnabled());
}

TEST_CASE("PlaylistEditor icon name is correct")
{
    MockConnection conn;
    PlaylistEditor editor(&conn);
    CHECK(editor.iconName() == "media-playlist");
}

TEST_CASE("PlaylistEditor lists are initially empty")
{
    MockConnection conn;
    PlaylistEditor editor(&conn);
    auto lists = editor.findChildren<QListWidget *>();
    REQUIRE_EQ(lists.size(), 2);

    // playlist list
    CHECK_EQ(lists[0]->count(), 0);
    // contents list
    CHECK_EQ(lists[1]->count(), 0);
}
