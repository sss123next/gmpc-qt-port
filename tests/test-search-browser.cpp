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
#include <QTreeView>

#include "browsers/search-browser.h"

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

