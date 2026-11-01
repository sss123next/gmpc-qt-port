#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-search-engine.cpp
 * @brief Юнит-тесты сборки поискового запроса (B3) и отмены ответов движком.
 *
 * Вся семантика «один any-tag вместо семи тегов», «ровно одна сортировка» и
 * «окно всегда» живёт в чистой makeSearchSteps(), поэтому проверяется без MPD;
 * сетевая часть (worker-поток) здесь не поднимается.
 */

#include "core/search-engine.h"

#include <QApplication>
#include <QSignalSpy>

// QSignalSpy::wait крутит событийный цикл — без приложения он бы не дождался.
static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QApplication app(argc, &argv);

namespace {

/// Настройки «сервера»: заведомо несуществующий unix-сокет, чтобы юнит-тесты
/// не ходили в живую базу (соединение падает сразу, скана базы нет).
SearchEngine::Endpoint testEndpoint()
{
    SearchEngine::Endpoint e;
    e.host = "/tmp/gmpc-qt-no-such-sock";
    e.port = 0;
    e.timeoutMs = 1000;
    return e;
}

int countOf(const QVector<SearchStep> &steps, SearchStep::Kind kind)
{
    int n = 0;
    for (const SearchStep &s : steps) {
        if (s.kind == kind) ++n;
    }
    return n;
}

const SearchStep *firstOf(const QVector<SearchStep> &steps, SearchStep::Kind kind)
{
    for (const SearchStep &s : steps) {
        if (s.kind == kind) return &s;
    }
    return nullptr;
}

} // namespace

TEST_SUITE("SearchEngine") {

TEST_CASE("clampedLimit keeps zero and oversized limits usable") {
    SearchQuery q;
    CHECK(q.clampedLimit() == SearchQuery::kDefaultLimit);

    q.limit = 50;
    CHECK(q.clampedLimit() == 50);

    q.limit = SearchQuery::kMaxLimit + 5000;
    CHECK(q.clampedLimit() == SearchQuery::kMaxLimit);

    q.limit = 0;
    CHECK(q.clampedLimit() == SearchQuery::kDefaultLimit);
}

TEST_CASE("empty term is not a valid query") {
    SearchQuery q;
    CHECK_FALSE(q.isValid());

    q.term = "   ";
    CHECK_FALSE(q.isValid());

    q.term = "а";
    CHECK(q.isValid());
}

TEST_CASE("\"all fields\" means one any-tag constraint, no tag constraints") {
    // Регрессия B3: семь AND-тегов дают пустой результат вместо поиска везде.
    SearchQuery q;
    q.term = "рамок";

    const QVector<SearchStep> steps = makeSearchSteps(q);
    CHECK(countOf(steps, SearchStep::Kind::AnyTag) == 1);
    CHECK(countOf(steps, SearchStep::Kind::Tag) == 0);

    const SearchStep *any = firstOf(steps, SearchStep::Kind::AnyTag);
    REQUIRE(any != nullptr);
    CHECK(any->value == "рамок");
}

TEST_CASE("a concrete field yields exactly one tag constraint") {
    struct Case { SearchQuery::Field field; enum mpd_tag_type tag; };
    const Case cases[] = {
        {SearchQuery::Field::Artist,      MPD_TAG_ARTIST},
        {SearchQuery::Field::AlbumArtist, MPD_TAG_ALBUM_ARTIST},
        {SearchQuery::Field::Title,       MPD_TAG_TITLE},
        {SearchQuery::Field::Album,       MPD_TAG_ALBUM},
        {SearchQuery::Field::Genre,       MPD_TAG_GENRE},
        {SearchQuery::Field::Composer,    MPD_TAG_COMPOSER},
        {SearchQuery::Field::Performer,   MPD_TAG_PERFORMER},
    };

    for (const Case &c : cases) {
        SearchQuery q;
        q.term = "test";
        q.field = c.field;

        const QVector<SearchStep> steps = makeSearchSteps(q);
        CHECK(countOf(steps, SearchStep::Kind::Tag) == 1);
        CHECK(countOf(steps, SearchStep::Kind::AnyTag) == 0);

        const SearchStep *tag = firstOf(steps, SearchStep::Kind::Tag);
        REQUIRE(tag != nullptr);
        CHECK(tag->tag == c.tag);
    }
}

TEST_CASE("searchFieldTag maps Any to MPD_TAG_COUNT") {
    CHECK(searchFieldTag(SearchQuery::Field::Any) == MPD_TAG_COUNT);
    CHECK(searchFieldTag(SearchQuery::Field::Title) == MPD_TAG_TITLE);
}

TEST_CASE("steps carry exactly one sort and a window covering the limit") {
    // Второй ограничитель сортировки сервер отвергает ACK «Unknown filter type».
    SearchQuery q;
    q.term = "a";
    q.offset = 200;
    q.limit = 100;

    const QVector<SearchStep> steps = makeSearchSteps(q);
    CHECK(countOf(steps, SearchStep::Kind::Sort) == 1);
    CHECK(countOf(steps, SearchStep::Kind::Window) == 1);

    const SearchStep *sort = firstOf(steps, SearchStep::Kind::Sort);
    REQUIRE(sort != nullptr);
    CHECK(sort->tag == MPD_TAG_ARTIST_SORT);
    CHECK_FALSE(sort->descending);

    const SearchStep *win = firstOf(steps, SearchStep::Kind::Window);
    REQUIRE(win != nullptr);
    CHECK(win->start == 200);
    CHECK(win->end == 300);
}

TEST_CASE("searchcount gets constraints only: no sort, no window") {
    // searchcount отвергает и sort, и window (ACK 2 «Unknown filter type»,
    // замер build/b3_count_probe.c), тогда как search их требует.
    SearchQuery q;
    q.term = "a";

    const QVector<SearchStep> countSteps = makeCountSteps(q);
    REQUIRE(countSteps.size() == 1);
    CHECK(countSteps.first().kind == SearchStep::Kind::AnyTag);
    CHECK(countOf(makeCountSteps(q), SearchStep::Kind::Sort) == 0);
    CHECK(countOf(makeCountSteps(q), SearchStep::Kind::Window) == 0);

    q.field = SearchQuery::Field::Album;
    const QVector<SearchStep> byAlbum = makeCountSteps(q);
    REQUIRE(byAlbum.size() == 1);
    CHECK(byAlbum.first().kind == SearchStep::Kind::Tag);
    CHECK(byAlbum.first().tag == MPD_TAG_ALBUM);

    // Пустой запрос не порождает и счётчик: сервер обошёл бы базу ради нуля строк.
    SearchQuery empty;
    empty.term = "   ";
    CHECK(makeCountSteps(empty).isEmpty());

    // Обычный поиск от этого не страдает — окно и сортировка на месте.
    const QVector<SearchStep> search = makeSearchSteps(q);
    CHECK(countOf(search, SearchStep::Kind::Sort) == 1);
    CHECK(countOf(search, SearchStep::Kind::Window) == 1);
}

TEST_CASE("sort direction and trimmed term reach the protocol steps") {
    SearchQuery q;
    q.term = "  night  ";
    q.sortAscending = false;

    const QVector<SearchStep> steps = makeSearchSteps(q);

    const SearchStep *any = firstOf(steps, SearchStep::Kind::AnyTag);
    REQUIRE(any != nullptr);
    CHECK(any->value == "night");

    const SearchStep *sort = firstOf(steps, SearchStep::Kind::Sort);
    REQUIRE(sort != nullptr);
    CHECK(sort->descending);
}

TEST_CASE("an empty query produces no protocol steps") {
    SearchQuery q;
    q.term = "\t \n";
    CHECK(makeSearchSteps(q).isEmpty());
}

TEST_CASE("window end never exceeds the clamped limit") {
    SearchQuery q;
    q.term = "a";
    q.limit = 100000;   // больше потолка — должен быть прижат kMaxLimit
    q.offset = 0;

    const QVector<SearchStep> steps = makeSearchSteps(q);
    const SearchStep *win = firstOf(steps, SearchStep::Kind::Window);
    REQUIRE(win != nullptr);
    CHECK(win->end == SearchQuery::kMaxLimit);
}

TEST_CASE("search is rejected without a term or server settings") {
    SearchEngine engine;
    // Endpoint не задан → запрос не должен даже ставиться в очередь.
    SearchQuery q;
    q.term = "a";
    CHECK(engine.search(q) == 0);

    engine.setEndpoint(testEndpoint());
    CHECK(engine.endpoint() == testEndpoint());
    SearchQuery empty;
    CHECK(engine.search(empty) == 0);

    // Отмена на покое безопасна.
    engine.cancel();
}

TEST_CASE("endpoint changes cancel the previous request") {
    SearchEngine engine;

    SearchEngine::Endpoint first = testEndpoint();
    engine.setEndpoint(first);
    const quint64 id = engine.search({QStringLiteral("a"), SearchQuery::Field::Any,
                                      0u, SearchQuery::kDefaultLimit, true});
    CHECK(id != 0);

    // Тот же endpoint ничего не отменяет: иначе каждый setEndpoint обнулял бы
    // живой поиск. Смена сервера — наоборот, ответ старого нам уже не нужен.
    engine.setEndpoint(first);
    SearchEngine::Endpoint other = testEndpoint();
    other.host = "/tmp/gmpc-qt-other-sock";
    engine.setEndpoint(other);
    CHECK(engine.endpoint() == other);

    engine.cancel();
}

/*
 * Интеграционная прогонка на живом MPD: проверяет то, что юнит-тестами не
 * покрывается — worker-поток с собственным соединением, чтение ответа и счётчик
 * searchcount. Выключена по умолчанию (CI без сервера), включается так:
 *   GMPC_QT_TEST_MPD=/home/sss/.mpd/sock ctest --test-dir build -R test-search-engine
 */
TEST_CASE("live MPD: windowed any-tag search returns hits and a total") {
    const QString socket = qEnvironmentVariable("GMPC_QT_TEST_MPD");
    if (socket.isEmpty()) {
        MESSAGE("GMPC_QT_TEST_MPD не задан — интеграционная проверка пропущена");
        return;
    }

    SearchEngine engine;
    SearchEngine::Endpoint e;
    e.host = socket;
    e.timeoutMs = 5000;
    engine.setEndpoint(e);

    QSignalSpy done(&engine, &SearchEngine::resultsReady);
    SearchQuery q;
    q.term = QStringLiteral("a");
    q.limit = 20;
    const quint64 id = engine.search(q);
    REQUIRE(id != 0);
    REQUIRE(done.wait(15000));

    const QList<QVariant> args = done.takeFirst();
    CHECK_EQ(args.at(0).toULongLong(), id);
    const QVector<SearchHit> hits = args.at(1).value<QVector<SearchHit>>();
    const int total = args.at(2).toInt();
    const QString error = args.at(3).toString();

    CHECK(error.isEmpty());
    CHECK_FALSE(hits.isEmpty());
    // Окно обязано быть соблюдено, а searchcount (без sort/window) — вернуть
    // полное число совпадений по всей базе.
    CHECK(static_cast<unsigned>(hits.size()) <= q.clampedLimit());
    CHECK(total > 0);
    CHECK(total >= hits.size());
    if (!hits.isEmpty()) {
        // qPrintable здесь недопустим: doctest вычисляет сообщение позже, чем живёт
        // временный QByteArray, — в отчёт попадает мусор.
        MESSAGE("получено " << hits.size() << " из " << total
                            << "; первый: " << hits.first().artist.toStdString()
                            << " — " << hits.first().title.toStdString());
    }

    // Отмена на живом запросе не должна оставлять соединение в error-state:
    // следующий поиск обязан отработать.
    engine.cancel();
    QSignalSpy second(&engine, &SearchEngine::resultsReady);
    q.term = QStringLiteral("karcev");
    REQUIRE(engine.search(q) != 0);
    REQUIRE(second.wait(15000));
    CHECK(second.at(0).at(3).toString().isEmpty());
}

} // TEST_SUITE
