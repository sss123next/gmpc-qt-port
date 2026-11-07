#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "trompeloeil-doctest.hpp"

/**
 * @file test-playlist-editor-commands.cpp
 * @brief Последовательности команд редактора сохранённых списков (Phase 7.5).
 *
 * Виджет проверяется кликами, а не только чистые функции: `MpdCommands` теперь
 * виртуальный, поэтому мок ловит именно то, что панель собирается отправить серверу.
 * Ни одно соединение здесь не нужно — команды подменены, ответы пустые.
 */

#include <QApplication>
#include <QListWidget>

#include "browsers/playlist-editor.h"
#include "core/mpd-commands.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QApplication app(argc, &argv);

// ── Моки: соединение (нужно конструктору) и команды (то, что проверяем) ─────

class MockConnection : public Connection {
public:
    MAKE_MOCK4(connectToServer, bool(const QString &, int, const QString &, int));
    MAKE_CONST_MOCK0(isConnected, bool());
    MAKE_MOCK0(fetchStatus, Connection::Status());
    MAKE_MOCK0(nextEntity, mpd_entity *());
};

class MockCommands : public MpdCommands {
public:
    explicit MockCommands(Connection *conn) : MpdCommands(conn) {}
    // Подменяются только те команды, которые редактор зовёт: остальные (addUri и пр.)
    // в этих путях не участвуют.
    MAKE_MOCK1(createEmptyStoredPlaylist, void(const QString &));
    MAKE_MOCK4(movePlaylistBlock, void(const QString &, int, int, int));
    MAKE_MOCK3(removePlaylistPositions, void(const QString &, int, int));
};

// ── helpers ─────────────────────────────────────────────────────────────────

namespace {

/** @brief Редактор с выделенным списком «list» и четырьмя позициями в нём.
 *
 *  Строки кладутся ПОСЛЕ setCurrentRow: выбор имени перечитывает содержимое
 *  (refreshContents), а оно у мока пустое — до команды панель пришлось бы наполнять
 *  заново. Ответы соединения разрешены «как угодно»: проверяем не их, а вызовы команд.
 */
void prepare(PlaylistEditor &editor)
{
    auto *names = editor.findChild<QListWidget *>(QStringLiteral("playlistNames"));
    REQUIRE(names != nullptr);
    new QListWidgetItem(QStringLiteral("list"), names);
    names->setCurrentRow(0);   // → onPlaylistSelected → refreshContents (пусто)

    auto *contents = editor.findChild<QListWidget *>(QStringLiteral("playlistContents"));
    REQUIRE(contents != nullptr);
    for (int i = 0; i < 4; ++i)
        new QListWidgetItem(QString::number(i), contents);
}

}  // namespace

TEST_CASE("MockCommands перехватывает виртуальные команды")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(true);

    MockCommands cmds(&conn);
    MpdCommands *base = &cmds;   // вызов через базовый указатель: проверяем сам override

    REQUIRE_CALL(cmds, createEmptyStoredPlaylist(QStringLiteral("x")));
    base->createEmptyStoredPlaylist(QStringLiteral("x"));
}

// ── «New…» без диалога: одна команда, ничего больше ─────────────────────────

TEST_CASE("Новый список создаётся пустым по имени")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(true);
    ALLOW_CALL(conn, nextEntity()).RETURN(nullptr);

    MockCommands cmds(&conn);
    PlaylistEditor editor(&conn, &cmds);

    REQUIRE_CALL(cmds, createEmptyStoredPlaylist(QStringLiteral("fresh")));
    editor.newPlaylistNamed(QStringLiteral("fresh"));
}

// ── Перемещение выделения: цель пересчитывается без вынутого блока ───────────

TEST_CASE("Перемещение вверх зовёт playlistmove с целью на строку выше")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(true);
    ALLOW_CALL(conn, nextEntity()).RETURN(nullptr);

    MockCommands cmds(&conn);
    PlaylistEditor editor(&conn, &cmds);
    prepare(editor);

    auto *names = editor.findChild<QListWidget *>(QStringLiteral("playlistNames"));
    REQUIRE(names != nullptr);
    CHECK_EQ(names->currentRow(), 0);

    auto *contents = editor.findChild<QListWidget *>(QStringLiteral("playlistContents"));
    CHECK_EQ(contents->count(), 4);
    contents->item(2)->setSelected(true);

    // Строка 2 встаёт на место строки 1: TO — координата без вынутого блока, то есть 1.
    REQUIRE_CALL(cmds, movePlaylistBlock(QStringLiteral("list"), 2, 1, 1));
    CHECK(QMetaObject::invokeMethod(&editor, "moveSelection", Qt::DirectConnection,
                                    Q_ARG(int, -1)));
}

TEST_CASE("Перемещение блока вниз считает цель за блоком")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(true);
    ALLOW_CALL(conn, nextEntity()).RETURN(nullptr);

    MockCommands cmds(&conn);
    PlaylistEditor editor(&conn, &cmds);
    prepare(editor);

    auto *contents = editor.findChild<QListWidget *>(QStringLiteral("playlistContents"));
    contents->item(0)->setSelected(true);
    contents->item(1)->setSelected(true);

    // Блок [0..2) сдвигается на строку вниз: destRow=3, TO = 3 − count = 1.
    REQUIRE_CALL(cmds, movePlaylistBlock(QStringLiteral("list"), 0, 2, 1));
    QMetaObject::invokeMethod(&editor, "moveSelection", Qt::DirectConnection,
                              Q_ARG(int, 1));
}

TEST_CASE("На краю списка перемещение не отправляет команду")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(true);
    ALLOW_CALL(conn, nextEntity()).RETURN(nullptr);

    MockCommands cmds(&conn);
    PlaylistEditor editor(&conn, &cmds);
    prepare(editor);

    auto *contents = editor.findChild<QListWidget *>(QStringLiteral("playlistContents"));
    contents->item(0)->setSelected(true);   // уже первая: вверх двигать некуда

    REQUIRE_CALL(cmds, movePlaylistBlock(QStringLiteral("list"), 0, 1, -999))
        .TIMES(0);
    QMetaObject::invokeMethod(&editor, "moveSelection", Qt::DirectConnection,
                              Q_ARG(int, -1));
}

TEST_CASE("Несмежное выделение не перемещается")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(true);
    ALLOW_CALL(conn, nextEntity()).RETURN(nullptr);

    MockCommands cmds(&conn);
    PlaylistEditor editor(&conn, &cmds);
    prepare(editor);

    auto *contents = editor.findChild<QListWidget *>(QStringLiteral("playlistContents"));
    contents->item(0)->setSelected(true);
    contents->item(2)->setSelected(true);   // между ними строка 1

    REQUIRE_CALL(cmds, movePlaylistBlock(QStringLiteral("list"), -999, -999, -999))
        .TIMES(0);
    QMetaObject::invokeMethod(&editor, "moveSelection", Qt::DirectConnection,
                              Q_ARG(int, 1));
}

// ── Удаление: блоки снизу вверх и один вызов на смежный участок ─────────────

TEST_CASE("Удаление отправляет по команде на смежный блок, снизу вверх")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(true);
    ALLOW_CALL(conn, nextEntity()).RETURN(nullptr);

    MockCommands cmds(&conn);
    PlaylistEditor editor(&conn, &cmds);
    prepare(editor);

    auto *contents = editor.findChild<QListWidget *>(QStringLiteral("playlistContents"));
    contents->item(0)->setSelected(true);   // блок [0..1]
    contents->item(1)->setSelected(true);
    contents->item(3)->setSelected(true);   // отдельная строка

    CHECK(editor.removalBlocks() == QVector<QPair<int, int>>{{3, 3}, {0, 1}});

    trompeloeil::sequence order;
    REQUIRE_CALL(cmds, removePlaylistPositions(QStringLiteral("list"), 3, 3))
        .IN_SEQUENCE(order);
    // Смежные строки идут одним диапазоном: два вызова на четыре строки — это уже
    // не «по одной команде на трэк».
    REQUIRE_CALL(cmds, removePlaylistPositions(QStringLiteral("list"), 0, 1))
        .IN_SEQUENCE(order);

    QMetaObject::invokeMethod(&editor, "removeCurrentPosition", Qt::DirectConnection);
}

TEST_CASE("Без выделения удаляется текущая строка")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(true);
    ALLOW_CALL(conn, nextEntity()).RETURN(nullptr);

    MockCommands cmds(&conn);
    PlaylistEditor editor(&conn, &cmds);
    prepare(editor);

    auto *contents = editor.findChild<QListWidget *>(QStringLiteral("playlistContents"));
    contents->clearSelection();
    contents->setCurrentRow(2);

    CHECK(editor.removalBlocks() == QVector<QPair<int, int>>{{2, 2}});
}