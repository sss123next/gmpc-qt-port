#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "trompeloeil-doctest.hpp"

/**
 * @file test-db-browser.cpp
 * @brief Тесты дерева базы, таблицы трэков и экрана Database.
 *
 * Сервер не нужен: транспорт (`MpdCommands`) подменён наследником с готовыми
 * ответами. Живые кейсы транспорта — в test-mpd-error/test-server-info-dialog,
 * здесь проверяются чистые функции, модель и проводка виджета.
 */

#include <QApplication>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QSplitter>
#include <QTableView>
#include <QThread>
#include <QTreeView>

#include "browsers/database-browser.h"
#include "browsers/database-track-model.h"
#include "browsers/music-database-model.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
// На куче и без delete: статический QApplication в Qt 6.12 падает в
// qt_cleanup_icon_cache при выходе (кэш иконок уже разобран).
static QApplication *app = new QApplication(argc, &argv);

namespace {

class MockConnection : public Connection {
public:
    MAKE_MOCK4(connectToServer, bool(const QString &, int, const QString &, int));
    MAKE_CONST_MOCK0(isConnected, bool());
};

/// Транспорт с готовыми ответами: ни одного обращения к сети.
class FakeCmds : public MpdCommands {
public:
    explicit FakeCmds(Connection *conn) : MpdCommands(conn) {}

    // Список тегов тоже подменён: настоящий метод полез бы в raw() мока.
    QVector<enum mpd_tag_type> supportedTags() const override
    {
        return {MPD_TAG_ARTIST, MPD_TAG_GENRE};
    }

    QVector<TagGroupStat> groupedCount(
        enum mpd_tag_type, const QVector<MpdTagConstraint> &constraints) const override
    {
        if (constraints.isEmpty())
            return groups;
        // Альбомы группы — по имени ограничения.
        auto it = albums.find(constraints.first().value);
        return it == albums.end() ? QVector<TagGroupStat>{} : *it;
    }

    QVector<DbSong> songsForConstraints(
        const QVector<MpdTagConstraint> &constraints, int) const override
    {
        if (constraints.isEmpty())
            return {};
        auto it = tracks.find(constraints.first().value);
        return it == tracks.end() ? QVector<DbSong>{} : *it;
    }

    QVector<TagGroupStat> groups;
    QHash<QString, QVector<TagGroupStat>> albums;
    QHash<QString, QVector<DbSong>> tracks;
};

/// Прокрутить очередь событий дольше дебаунса фильтра.
void waitDebounce()
{
    QThread::msleep(300);
    QCoreApplication::processEvents();
}

} // namespace

// ── Подписи объёма и пустых значений ───────────────────────────────────────

TEST_CASE("compactSpan formats seconds, minutes and hours")
{
    CHECK_EQ(compactSpan(0), QString());
    CHECK_EQ(compactSpan(5), "5 s");
    CHECK_EQ(compactSpan(59), "59 s");
    CHECK_EQ(compactSpan(60), "1 m");
    CHECK_EQ(compactSpan(125), "3 m");      // округление вверх
    CHECK_EQ(compactSpan(3600), "1 h 0 m");
    CHECK_EQ(compactSpan(7500), "2 h 5 m");
}

TEST_CASE("empty tag value gets a visible placeholder")
{
    CHECK_EQ(musicDbDisplayName(QString()), "(Unknown)");
    CHECK_EQ(musicDbDisplayName("Tool"), "Tool");
}

// ── Ограничения активации ────────────────────────────────────────────────

TEST_CASE("musicDbConstraints: group filters by the grouping tag")
{
    const auto c = musicDbConstraints(MPD_TAG_GENRE, MusicDbItem::Group,
                                      "Post-rock", {});
    REQUIRE_EQ(c.size(), 1);
    CHECK(c[0].tag == MPD_TAG_GENRE);
    CHECK(c[0].value == "Post-rock");

    // Пустое имя — легальная группа «без тега», а не отсутствие фильтра.
    const auto empty = musicDbConstraints(MPD_TAG_ARTIST, MusicDbItem::Group,
                                          {}, {});
    REQUIRE_EQ(empty.size(), 1);
    CHECK(empty[0].tag == MPD_TAG_ARTIST);
    CHECK(empty[0].value.isEmpty());
}

TEST_CASE("musicDbConstraints: album keeps its parent group")
{
    const auto c = musicDbConstraints(MPD_TAG_DATE, MusicDbItem::Album,
                                      "Ænima", "Tool");
    REQUIRE_EQ(c.size(), 2);
    CHECK(c[0].tag == MPD_TAG_DATE);      // тег группировки, а не всегда ARTIST
    CHECK(c[0].value == "Tool");
    CHECK(c[1].tag == MPD_TAG_ALBUM);
    CHECK(c[1].value == "Ænima");

    const auto orphan = musicDbConstraints(MPD_TAG_ARTIST, MusicDbItem::Album,
                                           "Ænima", {});
    REQUIRE_EQ(orphan.size(), 1);
    CHECK(orphan[0].tag == MPD_TAG_ALBUM);
}

// ── Модель дерева ─────────────────────────────────────────────────────────

TEST_CASE("MusicDatabaseModel is initially empty")
{
    MockConnection conn;
    FakeCmds cmds(&conn);
    MusicDatabaseModel model(&conn, &cmds);
    CHECK_EQ(model.rowCount({}), 0);
}

TEST_CASE("MusicDatabaseModel has name and stats columns")
{
    MockConnection conn;
    FakeCmds cmds(&conn);
    MusicDatabaseModel model(&conn, &cmds);
    CHECK_EQ(model.columnCount({}), 2);
    CHECK_FALSE(model.headerData(1, Qt::Horizontal).toString().isEmpty());
}

TEST_CASE("MusicDatabaseModel loadGroups is safe when disconnected")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(false);
    FakeCmds cmds(&conn);

    MusicDatabaseModel model(&conn, &cmds);
    model.loadGroups(MPD_TAG_GENRE);
    CHECK_EQ(model.rowCount({}), 0);
}

TEST_CASE("MusicDatabaseModel fills groups with server stats")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(true);
    FakeCmds cmds(&conn);
    cmds.groups = {{"Tool", 169, 63917}, {"Radiohead", 120, 40000}};

    MusicDatabaseModel model(&conn, &cmds);
    model.loadGroups(MPD_TAG_ARTIST);

    REQUIRE_EQ(model.rowCount({}), 2);
    const QModelIndex first = model.index(0, 1);
    CHECK(first.data(Qt::DisplayRole).toString().contains("169"));
    CHECK(first.data(MusicDbRoles::StatsRole).toString().contains("17 h"));

    // Сырое имя и объём отдаются правой панели без разбора подписей.
    const MusicDbRef ref = model.refFor(model.index(0, 0));
    REQUIRE(ref.valid);
    CHECK_EQ(ref.name, "Tool");
    CHECK_EQ(ref.songs, 169);
}

TEST_CASE("MusicDatabaseModel loads albums lazily with their stats")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(true);
    FakeCmds cmds(&conn);
    cmds.groups = {{"Tool", 10, 100}};
    cmds.albums["Tool"] = {{"Ænima", 8, 3000}, {"Lateralus", 2, 600}};

    MusicDatabaseModel model(&conn, &cmds);
    model.loadGroups(MPD_TAG_ARTIST);

    const QModelIndex group = model.index(0, 0);
    REQUIRE(model.canFetchMore(group));
    model.fetchMore(group);

    REQUIRE_EQ(model.rowCount(group), 2);
    CHECK_FALSE(model.canFetchMore(group));   // повторно не грузим
    CHECK_EQ(model.data(model.index(1, 0, group)).toString(), "Lateralus");
}

TEST_CASE("MusicDatabaseModel stays quiet without a connection")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(false);
    FakeCmds cmds(&conn);

    MusicDatabaseModel model(&conn, &cmds);
    CHECK_FALSE(model.canFetchMore({}));
}

// ── Таблица трэков ───────────────────────────────────────────────────────

TEST_CASE("DatabaseTrackModel exposes uri and sorts numerically by time")
{
    DatabaseTrackModel model;
    DbSong a; a.uri = "a.flac"; a.title = "A"; a.duration = 1000;
    DbSong b; b.uri = "b.flac"; b.title = "B"; b.duration = 9;
    model.setTracks({a, b});

    REQUIRE_EQ(model.rowCount(), 2);
    CHECK_EQ(model.data(model.index(1, 0), DatabaseTrackModel::UriRole)
                 .toString(), "b.flac");

    // «10:00» по тексту встало бы раньше «0:09» — сортировка обязана быть числовой.
    model.sort(DatabaseTrackModel::TimeColumn);
    CHECK_EQ(model.trackAt(0)->title, "B");
}

TEST_CASE("DatabaseTrackModel column keys roundtrip")
{
    for (int c = 0; c < DatabaseTrackModel::ColumnCount; ++c) {
        const QString key = DatabaseTrackModel::columnKey(c);
        CHECK_FALSE(key.isEmpty());
        CHECK_EQ(DatabaseTrackModel::columnForKey(key), c);
    }
    CHECK_EQ(DatabaseTrackModel::columnForKey("nope"), -1);
}

// ── Виджет экрана ────────────────────────────────────────────────────────

TEST_CASE("DatabaseBrowser has navigation tree and track table")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(false);
    FakeCmds cmds(&conn);

    DatabaseBrowser browser(&conn, &cmds);
    CHECK(browser.title() == "Database");
    REQUIRE(browser.treeView() != nullptr);
    REQUIRE(browser.trackView() != nullptr);
    // Двойной клик в дереве занят добавлением группы/альбома.
    CHECK_FALSE(browser.treeView()->expandsOnDoubleClick());
}

TEST_CASE("DatabaseBrowser loads groups on connect and shows tracks of a node")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(true);
    FakeCmds cmds(&conn);
    cmds.groups = {{"Tool", 2, 400}};
    DbSong s; s.uri = "a1.flac"; s.title = "Schism";
    s.artist = "Tool"; s.album = "Ænima"; s.track = "1"; s.duration = 200;
    cmds.tracks["Tool"] = {s};
    cmds.albums["Tool"] = {{"Ænima", 2, 400}};

    DatabaseBrowser browser(&conn, &cmds);
    browser.onConnectionChanged(true);

    auto *tree = browser.findChild<QTreeView *>("databaseTree");
    REQUIRE(tree != nullptr);
    // Дерево показывает прокси-модель: строка группы видна.
    CHECK_EQ(tree->model()->rowCount(), 1);

    // Раскрытие идёт через view → proxy → источник, а не напрямую в модель.
    const QModelIndex groupIdx = tree->model()->index(0, 0);
    REQUIRE(groupIdx.isValid());
    REQUIRE(tree->model()->canFetchMore(groupIdx));
    // Без hasChildren()==true вид не рисует стрелку и даже не спрашивает
    // canFetchMore — узел остаётся навсегда свёрнутым.
    CHECK(tree->model()->hasChildren(groupIdx));
    CHECK_FALSE(tree->isExpanded(groupIdx));   // вид ещё ничего не грузил
    tree->expand(groupIdx);
    waitDebounce();          // раскладка вида отложена до цикла событий
    CHECK_EQ(tree->model()->rowCount(groupIdx), 1);
    CHECK_FALSE(tree->model()->canFetchMore(groupIdx));

    tree->selectionModel()->setCurrentIndex(
        tree->model()->index(0, 0),
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QCoreApplication::processEvents();

    auto *table = browser.findChild<QTableView *>("databaseTracks");
    REQUIRE(table != nullptr);
    CHECK_EQ(table->model()->rowCount(), 1);

    const auto title =
        browser.findChild<QLabel *>("databaseHeaderTitle")->text();
    CHECK(title.contains("Tool"));
}

TEST_CASE("DatabaseBrowser filter narrows groups and keeps matches")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(true);
    FakeCmds cmds(&conn);
    cmds.groups = {{"Tool", 1, 100}, {"Radiohead", 2, 200}};

    DatabaseBrowser browser(&conn, &cmds);
    browser.onConnectionChanged(true);

    auto *tree = browser.findChild<QTreeView *>("databaseTree");
    REQUIRE(tree != nullptr);
    CHECK_EQ(tree->model()->rowCount(), 2);

    auto *filter = browser.findChild<QLineEdit *>("databaseFilter");
    REQUIRE(filter != nullptr);
    filter->setText("radio");
    waitDebounce();

    REQUIRE_EQ(tree->model()->rowCount(), 1);
    CHECK(tree->model()->index(0, 0).data().toString() == "Radiohead");

    filter->clear();
    waitDebounce();
    CHECK_EQ(tree->model()->rowCount(), 2);
}

TEST_CASE("DatabaseBrowser activation maps through the tree proxy")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(true);
    FakeCmds cmds(&conn);
    cmds.groups = {{"Tool", 1, 100}};

    DatabaseBrowser browser(&conn, &cmds);
    browser.onConnectionChanged(true);

    auto *tree = browser.findChild<QTreeView *>("databaseTree");
    REQUIRE(tree != nullptr);
    const QModelIndex idx = tree->model()->index(0, 0);
    REQUIRE(idx.isValid());
    tree->selectionModel()->setCurrentIndex(
        idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);

    const auto constraints = browser.activationFor(idx);
    REQUIRE_EQ(constraints.size(), 1);
    CHECK(constraints[0].value == "Tool");

    // Меню получает то же намерение.
    const auto intents = browser.selectedIntents();
    REQUIRE_EQ(intents.size(), 1);
}

// ── Живой MPD: серверные счётчики и выборка (только чтение) ─────────────

namespace {

const char *kSkipLive = "GMPC_QT_TEST_MPD не задан — интеграционная проверка пропущена";

bool openLive(Connection *conn)
{
    const QString socket = qEnvironmentVariable("GMPC_QT_TEST_MPD");
    if (socket.isEmpty())
        return false;
    return conn->connectToServer(socket, 0, {}, 5000);
}

} // namespace

TEST_CASE("живой MPD: groupedCount отдаёт группы со счётчиками")
{
    Connection conn;
    if (!openLive(&conn)) {
        MESSAGE(kSkipLive);
        return;
    }

    MpdCommands cmds(&conn);
    const auto groups = cmds.groupedCount(MPD_TAG_ARTIST, {});
    // Порог не по числу: у одноразового сервера всего три артиста.
    REQUIRE_FALSE(groups.isEmpty());

    // Пустое значение — легальная группа; у непустых счётчик обязан быть > 0.
    for (const TagGroupStat &g : groups) {
        CHECK_GE(g.songs, 1);
        if (!g.value.isEmpty())
            CHECK_FALSE(g.playTimeSec == 999999999u);   // поле прочитано
    }
}

TEST_CASE("живой MPD: альбомы и трэки читаются по ограничениям")
{
    Connection conn;
    if (!openLive(&conn)) {
        MESSAGE(kSkipLive);
        return;
    }

    MpdCommands cmds(&conn);
    const auto groups = cmds.groupedCount(MPD_TAG_ARTIST, {});
    REQUIRE_FALSE(groups.isEmpty());

    // Берём первую непустую группу: пустая тоже валидна, но её альбомы — край.
    QString group;
    for (const TagGroupStat &g : groups) {
        if (!g.value.isEmpty()) {
            group = g.value;
            break;
        }
    }
    REQUIRE_FALSE(group.isEmpty());

    const auto albums =
        cmds.groupedCount(MPD_TAG_ALBUM, {{MPD_TAG_ARTIST, group}});
    REQUIRE_FALSE(albums.isEmpty());

    // Без ограничений `find` сервер отвергает — как и пустой findadd, это no-op.
    CHECK(cmds.songsForConstraints({}).isEmpty());

    const DbSong probe = [&] {
        for (const TagGroupStat &a : albums) {
            if (!a.value.isEmpty()) {
                const auto songs = cmds.songsForConstraints(
                    {{MPD_TAG_ARTIST, group}, {MPD_TAG_ALBUM, a.value}});
                if (!songs.isEmpty())
                    return songs.first();
            }
        }
        return DbSong{};
    }();

    REQUIRE_FALSE(probe.uri.isEmpty());
    CHECK_EQ(probe.artist, group);
}

TEST_CASE("DatabaseBrowser is disabled without a connection")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(false);
    FakeCmds cmds(&conn);

    DatabaseBrowser browser(&conn, &cmds);
    browser.onConnectionChanged(true);
    CHECK(browser.isEnabled());
    browser.onConnectionChanged(false);
    CHECK_FALSE(browser.isEnabled());
}