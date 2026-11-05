#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "trompeloeil-doctest.hpp"

/**
 * @file test-browser-menu.cpp
 * @brief Состав контекстного меню браузеров и разбор путей (Phase 7.4).
 *
 * Меню строится из намерений чистой функцией, поэтому «что доступно этой строке»
 * проверяется без MPD: ни очередь, ни сервер здесь не нужны — только URI и настройка
 * «Music directory».
 */

#include <QApplication>
#include <QListWidget>
#include <QMenu>
#include <QStringList>
#include <QTemporaryDir>

#include "browsers/browser-intent.h"
#include "browsers/playlist-editor.h"
#include "core/mpd-commands.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QApplication app(argc, &argv);

// ── Mock Connection (PlaylistEditor конструктору нужен объект соединения) ────

class MockConnection : public Connection {
public:
    MAKE_MOCK4(connectToServer, bool(const QString &, int, const QString &, int));
    MAKE_CONST_MOCK0(isConnected, bool());
    MAKE_MOCK0(fetchStatus, Connection::Status());
    MAKE_MOCK0(nextEntity, mpd_entity *());
};

// ── helpers ──────────────────────────────────────────────────────────────────

static BrowserIntent song(const QString &uri)
{
    BrowserIntent i;
    i.kind = BrowserIntent::Kind::Song;
    i.uri = uri;
    return i;
}

static BrowserIntent dir(const QString &uri)
{
    BrowserIntent i;
    i.kind = BrowserIntent::Kind::Directory;
    i.uri = uri;
    return i;
}

static BrowserIntent playlist(const QString &name)
{
    BrowserIntent i;
    i.kind = BrowserIntent::Kind::StoredPlaylist;
    i.uri = name;
    return i;
}

static BrowserIntent group(bool withConstraints)
{
    BrowserIntent i;
    i.kind = BrowserIntent::Kind::TagGroup;
    if (withConstraints)
        i.constraints.append(MpdTagConstraint{MPD_TAG_ARTIST, "Miles Davis"});
    return i;
}

// ── Локальный путь из URI ────────────────────────────────────────────────────

TEST_CASE("Путь MPD становится локальным только при заданном каталоге музыки")
{
    CHECK(localPathForMpdUri("jazz/miles/kind.flac", "/music") == "/music/jazz/miles/kind.flac");
    // Ведущий слэш — шум: lsinfo отдаёт пути относительно music_directory без него.
    CHECK(localPathForMpdUri("/jazz/kind.flac", "/music") == "/music/jazz/kind.flac");
    CHECK(localPathForMpdUri("jazz/kind.flac", "/music/") == "/music/jazz/kind.flac");

    // Без каталога музыки абсолютный путь не восстановить: пункт «открыть папку»
    // должен выключиться, а не открыть чёрт знает что.
    CHECK(localPathForMpdUri("jazz/kind.flac", "").isEmpty());
    CHECK(localPathForMpdUri("", "/music").isEmpty());

    // Корень базы — это сам каталог музыки, отдельной строки «.» в меню нет.
    CHECK(localPathForMpdUri(".", "/music").isEmpty());
}

TEST_CASE("Путь, уходящий из каталога музыки, отвергается")
{
    // URI приходят с сервера; по этому пути потом откроют файловый менеджер.
    CHECK(localPathForMpdUri("../../etc/passwd", "/music").isEmpty());
    CHECK(localPathForMpdUri("jazz/../../../etc/passwd", "/music").isEmpty());
    // Префикс обязан быть именно каталогом, а не его буквенным началом.
    CHECK(localPathForMpdUri("../music2/a.flac", "/music").isEmpty());
}

// ── Состав меню ──────────────────────────────────────────────────────────────

TEST_CASE("Одиночный трэк играет, добавляет и меняет очередь")
{
    const BrowserMenuActions a = menuActionsFor({song("a/b/c.flac")}, "/music");
    CHECK(a.canPlay);
    CHECK(a.canAdd);
    CHECK(a.canReplace);
    CHECK(a.canCopyPath);
    CHECK(a.canOpenFolder);   // путь вычислился; существует ли каталог — решает меню
}

TEST_CASE("Несколько трэков не получают «играть»")
{
    const BrowserMenuActions a = menuActionsFor(
        {song("a/1.flac"), song("a/2.flac")}, "/music");
    CHECK_FALSE(a.canPlay);   // «сыграть выделенное» из десяти строк — это замена очереди
    CHECK(a.canAdd);
    CHECK(a.canReplace);
    CHECK(a.canCopyPath);
}

TEST_CASE("Каталог и сохранённый список добавляются, но не играются")
{
    const BrowserMenuActions d = menuActionsFor({dir("jazz/miles")}, "/music");
    CHECK_FALSE(d.canPlay);
    CHECK(d.canAdd);
    CHECK(d.canReplace);
    CHECK(d.canCopyPath);

    // У списка нет URI трэка: его «добавить» — это `load`, а не `addid`.
    const BrowserMenuActions p = menuActionsFor({playlist("road")}, "/music");
    CHECK_FALSE(p.canPlay);
    CHECK(p.canAdd);
}

TEST_CASE("Узел таг-браузера добавляется только со своими ограничениями")
{
    const BrowserMenuActions with = menuActionsFor({group(true)}, "/music");
    CHECK(with.canAdd);       // findadd поArtist=… — ровно как двойной клик
    CHECK(with.canReplace);
    CHECK_FALSE(with.canPlay);      // играть нечего: файла у группы нет
    CHECK_FALSE(with.canCopyPath);  // и путь показывать нечем
    CHECK_FALSE(with.canOpenFolder);

    // findadd без фильтра добавил бы всю базу — такой пункт лучше не показывать.
    const BrowserMenuActions without = menuActionsFor({group(false)}, "/music");
    CHECK_FALSE(without.canAdd);
    CHECK_FALSE(without.canReplace);
    CHECK_FALSE(without.canPlay);
}

TEST_CASE("Пустое выделение не даёт ни одного пункта")
{
    const BrowserMenuActions a = menuActionsFor({}, "/music");
    CHECK_FALSE(a.canPlay);
    CHECK_FALSE(a.canAdd);
    CHECK_FALSE(a.canReplace);
    CHECK_FALSE(a.canOpenFolder);
    CHECK_FALSE(a.canCopyPath);

    // Строка без URI (например, узел дерева-заглушка) — то же самое.
    const BrowserMenuActions empty = menuActionsFor({song(QString())}, "/music");
    CHECK_FALSE(empty.canAdd);
}

// ── Каталог для «открыть папку» и текст для «копировать путь» ────────────────

TEST_CASE("Открывается каталог трэка, а сам каталог — как есть")
{
    CHECK(folderToOpen({song("jazz/miles/kind.flac")}, "/music") == "/music/jazz/miles");
    CHECK(folderToOpen({dir("jazz/miles")}, "/music") == "/music/jazz/miles");
    CHECK(folderToOpen({group(true)}, "/music").isEmpty());
    // Трэк в корне базы живёт в самом каталоге музыки — открывать нечего было бы, если бы
    // это возвращало пусто.
    CHECK(folderToOpen({song("a.flac")}, "/music") == "/music");

    // Без каталога музыки нечего открывать даже при корректном URI.
    CHECK(folderToOpen({song("jazz/kind.flac")}, "").isEmpty());
}

TEST_CASE("В буфер уходит по строке на выделенную позицию")
{
    const QString text = copyTextFor({song("a/1.flac"), song("a/b/2.flac")}, "/music");
    CHECK(text == "/music/a/1.flac\n/music/a/b/2.flac");

    // Каталог музыки не задан — копируем URI так, как его зовёт MPD: `mpc add` понимает
    // именно его, а пустой пункт был бы обещанием несуществующего пути.
    CHECK(copyTextFor({song("a/1.flac")}, "") == "a/1.flac");

    // Группе путь показать нечем, но соседний трэк в выделении остаётся.
    CHECK(copyTextFor({group(true), song("a/2.flac")}, "/music") == "/music/a/2.flac");
    CHECK(copyTextFor({group(true)}, "").isEmpty());
}

// ── Пункты меню с реальными включениями ──────────────────────────────────────

TEST_CASE("«Открыть папку» включается только когда каталог действительно здесь")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QDir().mkpath(tmp.path() + "/jazz/miles");

    QMenu menu;
    const BrowserPathMenuItems here =
        addPathMenuItems(menu, {song("jazz/miles/kind.flac")}, tmp.path());
    REQUIRE(here.openFolder != nullptr);
    REQUIRE(here.copyPath != nullptr);
    CHECK(here.openFolder->isEnabled());
    CHECK(here.folder == tmp.path() + "/jazz/miles");
    CHECK(here.copyPath->isEnabled());

    // Тот же URI, но каталога на диске нет (сервер удалённый или настройки врёт).
    QMenu elsewhere;
    const BrowserPathMenuItems gone =
        addPathMenuItems(elsewhere, {song("jazz/miles/kind.flac")}, tmp.path() + "/nope");
    CHECK_FALSE(gone.openFolder->isEnabled());
    CHECK(gone.folder.isEmpty());   // открывать нечего, и лезть в openUrl нельзя
    CHECK_FALSE(gone.openFolder->toolTip().isEmpty());

    // Каталог музыки вообще не задан — подсказка обязана вести в Preferences.
    QMenu unset;
    const BrowserPathMenuItems noDir =
        addPathMenuItems(unset, {song("jazz/miles/kind.flac")}, QString());
    CHECK_FALSE(noDir.openFolder->isEnabled());
    CHECK(noDir.openFolder->toolTip().contains("Preferences"));

    // Копировать нечего: у таг-группы URI нет.
    QMenu groupOnly;
    const BrowserPathMenuItems grp = addPathMenuItems(groupOnly, {group(true)}, tmp.path());
    CHECK_FALSE(grp.copyPath->isEnabled());
}

// ── Позиции сохранённого списка ──────────────────────────────────────────────

TEST_CASE("Позиции сохранённого списка отдают свои URI")
{
    MockConnection conn;
    MpdCommands cmds(&conn);
    PlaylistEditor editor(&conn, &cmds);

    QListWidget *contents =
        editor.findChild<QListWidget *>(QStringLiteral("playlistContents"));
    REQUIRE(contents != nullptr);

    const QStringList uris{"a/1.flac", "b/2.flac", "c/3.flac"};
    for (const QString &uri : uris) {
        auto *item = new QListWidgetItem(uri, contents);
        item->setData(PlaylistEditor::UriRole, uri);
    }

    CHECK(editor.selectedIntents().isEmpty());   // выделения нет — меню не о чём строить

    contents->item(0)->setSelected(true);
    contents->item(2)->setSelected(true);
    const QVector<BrowserIntent> intents = editor.selectedIntents();
    REQUIRE_EQ(intents.size(), 2);
    QStringList got;
    for (const BrowserIntent &i : intents) {
        CHECK(i.kind == BrowserIntent::Kind::Song);
        got << i.uri;
    }
    // Порядок обхода выделения Qt не гарантирует, поэтому проверяем состав.
    std::sort(got.begin(), got.end());
    CHECK(got == QStringList{"a/1.flac", "c/3.flac"});
}

TEST_CASE("Позиции удаляются в убывающем порядке номеров")
{
    MockConnection conn;
    MpdCommands cmds(&conn);
    PlaylistEditor editor(&conn, &cmds);

    QListWidget *contents =
        editor.findChild<QListWidget *>(QStringLiteral("playlistContents"));
    REQUIRE(contents != nullptr);
    for (int i = 0; i < 5; ++i)
        new QListWidgetItem(QString::number(i), contents);

    // `playlistdelete` перенумеровывает список: восходящий порядок снёс бы не те трэки.
    contents->item(1)->setSelected(true);
    contents->item(3)->setSelected(true);
    contents->item(4)->setSelected(true);
    CHECK(editor.removalRows() == QList<int>{4, 3, 1});

    // Без выделения остаётся текущая строка — иначе Delete в никуда молчит.
    contents->clearSelection();
    contents->setCurrentRow(2);
    CHECK(editor.removalRows() == QList<int>{2});

    contents->setCurrentRow(-1);
    contents->clearSelection();
    CHECK(editor.removalRows().isEmpty());
}
