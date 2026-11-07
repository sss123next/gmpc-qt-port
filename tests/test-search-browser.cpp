#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "trompeloeil-doctest.hpp"

/**
 * @file test-search-browser.cpp
 * @brief Tests for SearchBrowser and SearchResultModel.
 *
 * Uses trompeloeil to mock Connection (no real MPD server needed).
 */

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
#include <QShortcut>
#include <QTreeView>

#include "browsers/search-browser.h"
#include "core/mpd-commands.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QApplication app(argc, &argv);

// ── Mock Connection / Commands ───────────────────────────────────────────────

class MockConnection : public Connection {
public:
    MAKE_MOCK4(connectToServer, bool(const QString &, int, const QString &, int));
    MAKE_CONST_MOCK0(isConnected, bool());
    MAKE_MOCK0(fetchStatus, Connection::Status());
    MAKE_MOCK0(nextEntity, mpd_entity *());
};

/** @brief Подменённые команды очереди: Enter/Insert в результатах — это они.
 *
 *  Мок ловит именно отправляемое, а не догадку о протоколе (движок поиска ходит
 *  своим соединением и здесь не участвует).
 */
class MockCommands : public MpdCommands {
public:
    explicit MockCommands(Connection *conn) : MpdCommands(conn) {}
    MAKE_MOCK1(addUri, void(const QString &));
    MAKE_MOCK0(clearQueue, void());
    MAKE_MOCK0(play, void());
};

// ── Tests for SearchResultModel ──────────────────────────────────────────────

TEST_CASE("SearchResultModel is initially empty")
{
    SearchResultModel model;
    CHECK_EQ(model.rowCount({}), 0);
    CHECK_EQ(model.columnCount({}), SearchResultModel::ColCount);
}

TEST_CASE("SearchResultModel column count is 5")
{
    SearchResultModel model;
    CHECK_EQ(model.columnCount({}), 5); // Artist, Title, Album, Genre, Length
}

TEST_CASE("SearchResultModel header returns non-empty labels")
{
    SearchResultModel model;
    for (int c = 0; c < SearchResultModel::ColCount; ++c) {
        auto h = model.headerData(c, Qt::Horizontal);
        CHECK_FALSE(h.toString().isEmpty());
    }
}

TEST_CASE("SearchResultModel setResults populates rows")
{
    SearchResultModel model;
    QVector<SearchResultItem> items;
    items.append({"/music/song1.mp3", "Artist A", "Song 1", "Album X", "Rock", 240});
    items.append({"/music/song2.mp3", "Artist B", "Song 2", "Album Y", "Jazz", 180});
    model.setResults(items);

    CHECK_EQ(model.rowCount({}), 2);
    CHECK_EQ(model.data(model.index(0, SearchResultModel::ColArtist)).toString(), "Artist A");
    CHECK_EQ(model.data(model.index(0, SearchResultModel::ColTitle)).toString(), "Song 1");
    CHECK_EQ(model.data(model.index(1, SearchResultModel::ColAlbum)).toString(), "Album Y");
    CHECK_EQ(model.data(model.index(1, SearchResultModel::ColGenre)).toString(), "Jazz");
}

TEST_CASE("SearchResultModel duration format")
{
    SearchResultModel model;
    QVector<SearchResultItem> items;
    items.append({"/x.mp3", "A", "S", "Al", "R", 125}); // 2:05
    model.setResults(items);
    auto dur = model.data(model.index(0, SearchResultModel::ColDuration)).toString();
    CHECK_EQ(dur, "2:05");
}

TEST_CASE("SearchResultModel resultAt returns correct item")
{
    SearchResultModel model;
    QVector<SearchResultItem> items;
    items.append({"/a.mp3", "Artist", "Title", "Album", "Genre", 300});
    model.setResults(items);

    auto *r = model.resultAt(0);
    REQUIRE(r != nullptr);
    CHECK_EQ(r->artist, "Artist");
    CHECK_EQ(r->title, "Title");
    CHECK_EQ(r->album, "Album");
    CHECK_EQ(r->genre, "Genre");
    CHECK_EQ(r->duration, 300);
}

TEST_CASE("SearchResultModel resultAt out-of-bounds returns null")
{
    SearchResultModel model;
    CHECK(model.resultAt(0) == nullptr);
    CHECK(model.resultAt(-1) == nullptr);
}

TEST_CASE("SearchResultModel safe with invalid index")
{
    SearchResultModel model;
    // All these should be quiet
    CHECK(model.data({}).isNull());
    CHECK(model.data(model.index(-1, 0)).isNull());
    CHECK(model.data(model.index(0, -1)).isNull());
    CHECK(model.data(model.index(0, 99)).isNull());
}

// ── Tests for SearchBrowser (widget) ─────────────────────────────────────────

TEST_CASE("SearchBrowser is a BrowserWidget subclass")
{
    MockConnection conn;
    SearchBrowser browser(&conn);
    CHECK(browser.title() == "Search");

    // Enabled state is owned by BrowserStack, so a bare browser starts enabled.
    CHECK(browser.isEnabled());
}

TEST_CASE("SearchBrowser contains QLineEdit and QTreeView")
{
    MockConnection conn;
    SearchBrowser browser(&conn);
    auto *edit = browser.findChild<QLineEdit *>();
    CHECK(edit != nullptr);
    CHECK_FALSE(edit->placeholderText().isEmpty());

    auto *view = browser.findChild<QTreeView *>();
    CHECK(view != nullptr);
    CHECK(view->model() != nullptr);
}

TEST_CASE("SearchBrowser reacts to connection state")
{
    MockConnection conn;
    SearchBrowser browser(&conn);
    browser.onConnectionChanged(true);
    CHECK(browser.isEnabled());
    browser.onConnectionChanged(false);
    CHECK_FALSE(browser.isEnabled());
}

TEST_CASE("SearchBrowser icon name is correct")
{
    MockConnection conn;
    SearchBrowser browser(&conn);
    CHECK(browser.iconName() == "edit-find");
}

// ── Поиск через движок (B3): запрос вне GUI-потока, результат из сигнала ─────

namespace {

/// Подписывает в модель ровно то, что прислал бы worker.
SearchHit makeHit(const QString &uri, const QString &artist, const QString &title)
{
    SearchHit h;
    h.uri = uri;
    h.artist = artist;
    h.title = title;
    h.album = "Album";
    h.genre = "Rock";
    h.duration = 200;
    return h;
}

QLabel *countLabel(SearchBrowser &browser)
{
    auto labels = browser.findChildren<QLabel *>();
    return labels.isEmpty() ? nullptr : labels.first();
}

} // namespace

TEST_CASE("SearchBrowser starts with an empty result and a hint")
{
    MockConnection conn;
    SearchBrowser browser(&conn);
    auto *view = browser.treeView();
    REQUIRE(view->model() != nullptr);
    CHECK_EQ(view->model()->rowCount({}), 0);

    QLabel *label = countLabel(browser);
    REQUIRE(label != nullptr);
    CHECK_FALSE(label->text().isEmpty());
}

TEST_CASE("results arriving from the worker fill the model and the counter")
{
    MockConnection conn;
    SearchBrowser browser(&conn);
    auto *model = browser.treeView()->model();

    // total > числа строк — значит за окном ещё есть совпадения (searchcount).
    Q_EMIT browser.engine()->resultsReady(1, {makeHit("/a.mp3", "A", "T1"),
                                             makeHit("/b.mp3", "B", "T2")},
                                         5700, QString());

    CHECK_EQ(model->rowCount({}), 2);
    CHECK_EQ(model->data(model->index(0, SearchResultModel::ColArtist)).toString(),
             "A");
    CHECK_EQ(model->data(model->index(1, SearchResultModel::ColTitle)).toString(),
             "T2");

    QLabel *label = countLabel(browser);
    REQUIRE(label != nullptr);
    CHECK(label->text().contains("5700"));
}

TEST_CASE("an engine error clears stale rows and reports the reason")
{
    MockConnection conn;
    SearchBrowser browser(&conn);
    auto *model = browser.treeView()->model();

    Q_EMIT browser.engine()->resultsReady(1, {makeHit("/a.mp3", "A", "T")}, 1,
                                         QString());
    REQUIRE_EQ(model->rowCount({}), 1);

    Q_EMIT browser.engine()->resultsReady(2, {}, -1, QStringLiteral("boom"));
    CHECK_EQ(model->rowCount({}), 0);
    CHECK(countLabel(browser)->text().contains("boom"));
}

TEST_CASE("empty input never reaches the engine and clears the screen") {
    MockConnection conn;
    SearchBrowser browser(&conn);
    auto *model = browser.treeView()->model();
    auto *edit = browser.findChild<QLineEdit *>();
    REQUIRE(edit != nullptr);

    Q_EMIT browser.engine()->resultsReady(1, {makeHit("/a.mp3", "A", "T")}, 1,
                                         QString());
    REQUIRE_EQ(model->rowCount({}), 1);

    // Пробел — не запрос: сервер обошёл бы базу целиком ради нуля строк.
    edit->setText("   ");
    QCoreApplication::processEvents();
    CHECK_EQ(model->rowCount({}), 0);
}

TEST_CASE("leaving the screen cancels the pending search")
{
    MockConnection conn;
    SearchBrowser browser(&conn);
    auto *edit = browser.findChild<QLineEdit *>();
    edit->setText("рамок");   // ставит дебаунс-таймер в ожидание

    browser.onDeactivate();   // не должно ни крашнуться, ни оставить запроса
    CHECK_EQ(browser.treeView()->model()->rowCount({}), 0);
}

// ── История сессии и действия по клавишам (7.6) ─────────────────────────────

namespace {

/// Наполняет вид результатом «как от движка»: строки + счётчик всего найденного.
void feed(SearchBrowser &browser, const QVector<SearchHit> &hits, int total)
{
    Q_EMIT browser.engine()->resultsReady(1, hits, total, QString());
}

/// Выделяет строку целиком (колонка 0 — по ней работает uniqueSelectedRows).
void selectRow(QTreeView *view, int row)
{
    // Флаг Rows обязателен: selectedRows(0) отдаёт строки только при выделении всей
    // строки, а прямой вызов selectionModel()->select() поведение вида не применяет.
    view->selectionModel()->select(view->model()->index(row, 0),
                                   QItemSelectionModel::Select
                                       | QItemSelectionModel::Rows);
}

} // namespace

TEST_CASE("Ctrl+↑ возвращает предыдущий термин, Ctrl+↓ — обратно")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(true);
    MpdCommands cmds(&conn);   // история живёт в виджете: команды не нужны
    SearchBrowser browser(&conn, &cmds);

    auto *edit = browser.findChild<QLineEdit *>();
    REQUIRE(edit != nullptr);

    edit->setText("alpha");
    QMetaObject::invokeMethod(&browser, "executeSearch", Qt::DirectConnection);
    edit->setText("beta");
    QMetaObject::invokeMethod(&browser, "executeSearch", Qt::DirectConnection);

    // Первый Ctrl+↑ — предыдущий термин (не тот, что набран сейчас).
    CHECK(QMetaObject::invokeMethod(&browser, "recallPreviousQuery",
                                    Qt::DirectConnection));
    CHECK_EQ(edit->text(), QStringLiteral("alpha"));

    QMetaObject::invokeMethod(&browser, "recallNextQuery", Qt::DirectConnection);
    CHECK_EQ(edit->text(), QStringLiteral("beta"));
}

TEST_CASE("повторный поиск того же термина не плодит историю")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(true);
    MpdCommands cmds(&conn);
    SearchBrowser browser(&conn, &cmds);

    auto *edit = browser.findChild<QLineEdit *>();
    REQUIRE(edit != nullptr);

    edit->setText("one");
    QMetaObject::invokeMethod(&browser, "executeSearch", Qt::DirectConnection);
    // Тот же термин дважды: в истории он один — иначе Ctrl+↑ стоял бы на месте.
    QMetaObject::invokeMethod(&browser, "executeSearch", Qt::DirectConnection);

    edit->setText("two");
    QMetaObject::invokeMethod(&browser, "executeSearch", Qt::DirectConnection);
    CHECK(QMetaObject::invokeMethod(&browser, "recallPreviousQuery",
                                    Qt::DirectConnection));
    CHECK_EQ(edit->text(), QStringLiteral("one"));
}

TEST_CASE("Enter/Insert на результатах кладут выделенные строки в очередь")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(true);
    MockCommands cmds(&conn);

    SearchBrowser browser(&conn, &cmds);
    feed(browser, {makeHit("/a.mp3", "A", "T1"), makeHit("/b.mp3", "B", "T2")}, 0);

    QTreeView *view = browser.treeView();
    REQUIRE_EQ(view->model()->rowCount({}), 2);
    selectRow(view, 1);   // только вторая строка
    CHECK_EQ(view->selectionModel()->selectedIndexes().size(), SearchResultModel::ColCount);
    CHECK_EQ(browser.selectedIntents().size(), 1);

    trompeloeil::sequence seq;
    REQUIRE_CALL(cmds, addUri(QStringLiteral("/b.mp3"))).IN_SEQUENCE(seq);

    QMetaObject::invokeMethod(&browser, "addSelectionToQueue", Qt::DirectConnection);
}

TEST_CASE("Ctrl+Insert заменяет очередь: clear → add выделенного → play")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(true);
    MockCommands cmds(&conn);

    SearchBrowser browser(&conn, &cmds);
    feed(browser, {makeHit("/a.mp3", "A", "T1"), makeHit("/b.mp3", "B", "T2")}, 0);

    QTreeView *view = browser.treeView();
    REQUIRE_EQ(view->model()->rowCount({}), 2);
    selectRow(view, 0);
    selectRow(view, 1);

    // Порядок важен: очистка до добавления, игра — после (иначе заиграла бы старая очередь).
    trompeloeil::sequence seq;
    REQUIRE_CALL(cmds, clearQueue()).IN_SEQUENCE(seq);
    REQUIRE_CALL(cmds, addUri(QStringLiteral("/a.mp3"))).IN_SEQUENCE(seq);
    REQUIRE_CALL(cmds, addUri(QStringLiteral("/b.mp3"))).IN_SEQUENCE(seq);
    REQUIRE_CALL(cmds, play()).IN_SEQUENCE(seq);

    QMetaObject::invokeMethod(&browser, "replaceQueueWithSelection",
                              Qt::DirectConnection);
}

TEST_CASE("без команд клавиши результата молчат")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(true);
    SearchBrowser browser(&conn);   // cmds == nullptr

    feed(browser, {makeHit("/a.mp3", "A", "T")}, 0);
    QTreeView *view = browser.treeView();
    REQUIRE_EQ(view->model()->rowCount({}), 1);
    selectRow(view, 0);

    CHECK(QMetaObject::invokeMethod(&browser, "addSelectionToQueue",
                                    Qt::DirectConnection));
}

TEST_CASE("прокрутка до конца догружает следующее окно и дописывает строки")
{
    MockConnection conn;
    ALLOW_CALL(conn, isConnected()).RETURN(true);
    MpdCommands cmds(&conn);

    SearchBrowser browser(&conn, &cmds);
    // Вид без показа не имеет размера — ползунку нужна раскладка.
    browser.resize(400, 200);
    browser.show();
    QCoreApplication::processEvents();   // дать layout пройти: иначе viewport нулевой

    auto *edit = browser.findChild<QLineEdit *>();
    REQUIRE(edit != nullptr);
    edit->setText("term");
    QMetaObject::invokeMethod(&browser, "executeSearch", Qt::DirectConnection);

    // Первое окно приходит целиком (200 строк) — значит за ним может быть ещё.
    const unsigned limit = SearchQuery{}.clampedLimit();
    QVector<SearchHit> page;
    for (unsigned i = 0; i < limit; ++i)
        page.append(makeHit(QStringLiteral("/p%1.mp3").arg(i), "A", "T"));
    feed(browser, page, static_cast<int>(limit) + 5);
    CHECK_EQ(static_cast<unsigned>(browser.shownCount()), limit);

    QCoreApplication::processEvents();   // вид пересчитает диапазон ползунка
    auto *tree = browser.treeView();
    REQUIRE(tree->viewport()->height() > 0);   // раскладка дала высоту
    CHECK_EQ(static_cast<unsigned>(browser.shownCount()), limit);

    QScrollBar *bar = tree->verticalScrollBar();
    REQUIRE(bar->maximum() > 0);   // есть куда прокручивать
    bar->setValue(bar->maximum());

    // Догрузка просит следующий offset, а не тот же самый.
    CHECK_EQ(browser.lastRequested().offset, limit);

    QVector<SearchHit> tail{makeHit("/tail.mp3", "Z", "T")};
    feed(browser, tail, static_cast<int>(limit) + 5);
    // Строки дописаны к прежним (не заменены): прокрутка стоит у нижней границы.
    CHECK_EQ(static_cast<unsigned>(browser.shownCount()), limit + 1);

    browser.hide();
}

TEST_CASE("прокси сортирует длительность числом, а не строкой")
{
    SearchResultModel model;
    QVector<SearchResultItem> items;

    // «10:00» против «9:59»: по тексту десятиминутный встал бы раньше.
    for (const auto &pair : {std::pair<int, const char *>{600, "long"},
                             {599, "short"}}) {
        SearchResultItem it;
        it.artist = pair.second;
        it.duration = pair.first;
        items.append(it);
    }
    model.setResults(items);

    SearchResultProxy proxy;
    proxy.setSourceModel(&model);
    proxy.sort(SearchResultModel::ColDuration, Qt::AscendingOrder);
    CHECK_EQ(proxy.index(0, SearchResultModel::ColArtist).data().toString(),
             QStringLiteral("short"));

    // Обычные колонки — без регистра: «ABBA» не должен уезжать от «abba».
    QVector<SearchResultItem> names;
    for (const char *artist : {"ABBA", "abba2"}) {
        SearchResultItem it;
        it.artist = artist;
        names.append(it);
    }
    model.setResults(names);
    proxy.sort(SearchResultModel::ColArtist, Qt::AscendingOrder);
    CHECK_EQ(proxy.index(0, SearchResultModel::ColArtist).data().toString(),
             QStringLiteral("ABBA"));
}

