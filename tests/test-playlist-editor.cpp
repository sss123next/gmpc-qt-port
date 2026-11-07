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
#include <QKeySequence>
#include <QListWidget>
#include <QPushButton>
#include <QShortcut>

#include "browsers/playlist-editor.h"
#include "core/mpd-commands.h"

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
    MpdCommands cmds(&conn);
    PlaylistEditor editor(&conn, &cmds);
    CHECK(editor.title() == "Playlists");

    // BrowserStack decides enabled state from the connection, not the widget.
    CHECK(editor.isEnabled());
}

TEST_CASE("PlaylistEditor contains two QListWidgets")
{
    MockConnection conn;
    MpdCommands cmds(&conn);
    PlaylistEditor editor(&conn, &cmds);
    auto lists = editor.findChildren<QListWidget *>();
    CHECK_EQ(lists.size(), 2);
}

TEST_CASE("PlaylistEditor contains four QPushButtons")
{
    MockConnection conn;
    MpdCommands cmds(&conn);
    PlaylistEditor editor(&conn, &cmds);
    auto btns = editor.findChildren<QPushButton *>();
    // New, Rename, Delete, Load
    CHECK_EQ(btns.size(), 4);
}

TEST_CASE("PlaylistEditor reacts to connection state")
{
    MockConnection conn;
    // isConnected() виртуальна и мок теперь действительно опрашивается; «подключено»
    // здесь про панель — у мока нет handle, поэтому чтения списков не происходит.
    ALLOW_CALL(conn, isConnected()).RETURN(true);
    MpdCommands cmds(&conn);
    PlaylistEditor editor(&conn, &cmds);
    editor.onConnectionChanged(true);
    CHECK(editor.isEnabled());
    editor.onConnectionChanged(false);
    CHECK_FALSE(editor.isEnabled());
}

TEST_CASE("PlaylistEditor icon name is correct")
{
    MockConnection conn;
    MpdCommands cmds(&conn);
    PlaylistEditor editor(&conn, &cmds);
    CHECK(editor.iconName() == "media-playlist");
}

TEST_CASE("PlaylistEditor lists are initially empty")
{
    MockConnection conn;
    MpdCommands cmds(&conn);
    PlaylistEditor editor(&conn, &cmds);
    auto lists = editor.findChildren<QListWidget *>();
    REQUIRE_EQ(lists.size(), 2);

    // playlist list
    CHECK_EQ(lists[0]->count(), 0);
    // contents list
    CHECK_EQ(lists[1]->count(), 0);
}

// ── Позиции сохранённого списка: активация и удаление ───────────────────────

TEST_CASE("PlaylistEditor wires activation and removal on the contents list")
{
    MockConnection conn;
    MpdCommands cmds(&conn);
    PlaylistEditor editor(&conn, &cmds);

    // Обе панели — QListWidget, а порядок потомков задаёт раскладка (виджеты переезжают
    // в обёртки сплиттера), поэтому искать надо по имени, а не по индексу.
    QListWidget *contents =
        editor.findChild<QListWidget *>(QStringLiteral("playlistContents"));
    REQUIRE(contents != nullptr);

    CHECK(contents->contextMenuPolicy() == Qt::CustomContextMenu);

    const auto shortcuts = contents->findChildren<QShortcut *>();
    REQUIRE_EQ(shortcuts.size(), 2);   // Delete + Backspace
    // В Qt6 QShortcut::key() возвращает целую последовательность, а не одну клавишу.
    const QKeySequence del(QKeySequence::Delete);
    const QKeySequence back(QKeySequence::Backspace);
    for (const QShortcut *s : shortcuts) {
        const bool known = s->key() == del || s->key() == back;
        CHECK(known);
        // Область действия — только список позиций: на весь стек браузеров эти
        // клавиши перехватывать нельзя, они нужны другим панелям.
        CHECK(s->context() == Qt::WidgetWithChildrenShortcut);
    }
    CHECK_NE(shortcuts[0]->key(), shortcuts[1]->key());

    // Клавиши живут на позициях списка, а не на панели с его именами: там Delete означал
    // бы удаление файла. Меню же есть у обеих панелей — только про разное: у имён пункты
    // про сами списки (load / replace / new / rename / delete), у позиций — про трэки.
    QListWidget *names =
        editor.findChild<QListWidget *>(QStringLiteral("playlistNames"));
    REQUIRE(names != nullptr);
    CHECK(names->contextMenuPolicy() == Qt::CustomContextMenu);
    CHECK(names->findChildren<QShortcut *>().isEmpty());

    // Выделение несколькими строками обязательно: «добавить в очередь» и «копировать
    // путь» применяются ко всей выборке, как в оригинальном тег-браузере.
    CHECK(contents->selectionMode() == QAbstractItemView::ExtendedSelection);
}

TEST_CASE("PlaylistEditor drops contents when the connection goes away")
{
    MockConnection conn;
    MpdCommands cmds(&conn);
    PlaylistEditor editor(&conn, &cmds);

    QListWidget *names =
        editor.findChild<QListWidget *>(QStringLiteral("playlistNames"));
    QListWidget *contents =
        editor.findChild<QListWidget *>(QStringLiteral("playlistContents"));
    REQUIRE(names != nullptr);
    REQUIRE(contents != nullptr);
    new QListWidgetItem("stale row", contents);
    REQUIRE_EQ(contents->count(), 1);

    // Строки чужого списка не должны переживать разрыв: их URI больше не о чём спрашивать.
    editor.onConnectionChanged(false);
    CHECK_EQ(names->count(), 0);
    CHECK_EQ(contents->count(), 0);
}
