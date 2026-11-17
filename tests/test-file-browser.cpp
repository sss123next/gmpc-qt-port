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
#include <QString>
#include <QTreeView>

#include "core/connection.h"

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
    // Мок опрашивается по-настоящему (isConnected виртуальна), но handle у него нет:
    // загрузка каталога пропускается, а состояние панели от соединения не зависит.
    ALLOW_CALL(conn, isConnected()).RETURN(true);
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

// ── Живой MPD: дерево файлов (политика тестирования, пункт «интеграция») ───

namespace {

const char *kSkipLive = "GMPC_QT_TEST_MPD не задан — интеграционная проверка пропущена";

/// Открывает настоящее соединение по адресу из окружения; false — сервера нет.
bool openLiveConnection(Connection *conn)
{
    const QString socket = qEnvironmentVariable("GMPC_QT_TEST_MPD");
    if (socket.isEmpty())
        return false;
    return conn->connectToServer(socket, 0, {}, 5000);
}

} // namespace

TEST_CASE("живой MPD: корень дерева файлов непуст и показывает basename")
{
    Connection conn;
    if (!openLiveConnection(&conn)) {
        MESSAGE(kSkipLive);
        return;
    }

    FileSystemModel model(&conn);
    REQUIRE(model.canFetchMore({}));
    model.fetchMore({});

    // Запрет «пусто = зелёный»: каталог музыки сервера обязан что-то отдать.
    const int rows = model.rowCount({});
    CHECK_GT(rows, 0);
    for (int i = 0; i < rows && i < 20; ++i) {
        const QModelIndex idx = model.index(i, 0);
        REQUIRE(idx.isValid());
        const QString path =
            model.data(idx, FileSystemModel::UriRole).toString();
        CHECK_FALSE(path.isEmpty());
        // Имя узла — последний компонент пути (замер №3b: MPD отдаёт путь целиком).
        CHECK_EQ(model.data(idx).toString(), mpdBasename(path));
    }
}

TEST_CASE("живой MPD: спуск на уровень не выходит за префикс родителя")
{
    Connection conn;
    if (!openLiveConnection(&conn)) {
        MESSAGE(kSkipLive);
        return;
    }

    FileSystemModel model(&conn);
    model.fetchMore({});

    int dirRow = -1;
    const int rows = model.rowCount({});
    for (int i = 0; i < rows && dirRow < 0; ++i) {
        if (model.data(model.index(i, 0), FileSystemModel::TypeRole).toInt()
            == static_cast<int>(FileNode::Directory))
            dirRow = i;
    }
    REQUIRE_MESSAGE(dirRow >= 0, "в корне сервера нет ни одного каталога");

    const QModelIndex parentIdx = model.index(dirRow, 0);
    REQUIRE(parentIdx.isValid());
    const QString parentPath =
        model.data(parentIdx, FileSystemModel::UriRole).toString();
    CHECK_FALSE(parentPath.isEmpty());

    // Дерево обязано спускаться ниже корня (B4), а не показывать одни папки.
    REQUIRE(model.canFetchMore(parentIdx));
    model.fetchMore(parentIdx);
    const int childRows = model.rowCount(parentIdx);
    CHECK_GT(childRows, 0);

    for (int i = 0; i < childRows && i < 20; ++i) {
        const QModelIndex idx = model.index(i, 0, parentIdx);
        REQUIRE(idx.isValid());
        const QString path =
            model.data(idx, FileSystemModel::UriRole).toString();
        // Записи внутри папки приходят уже с префиксом родителя (замер №3b), а
        // имя узла — только хвост: дублирования пути в имени быть не должно.
        CHECK(path.startsWith(parentPath + QLatin1Char('/')));
        CHECK_EQ(model.data(idx).toString(), mpdBasename(path));
    }
}
