#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-queue-diff.cpp
 * @brief Слияние ответа `plchanges` с локальной копией очереди и синхронизатор.
 *
 * Кейсы повторяют замеры на одноразовом MPD 0.25 (очередь из шести треков, сокет в
 * /tmp): дифф разреженный — правка тегов в середине даёт одну запись; удалённый трэк
 * в ответе не упоминается вовсе; после `clear` оба diff-запроса пусты при длине 0.
 * Именно эти три особенности и ломают наивное «префикс + дифф», так что проверяем их
 * отдельно от сервера — чистая функция ничего о сети не знает.
 */

#include "core/queue-diff.h"
#include "core/queue-sync.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <mpd/client.h>

// QSignalSpy::wait крутит событийный цикл — без приложения он бы не дождался.
static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QCoreApplication app(argc, &argv);

namespace {

SongEntry song(int id, int pos, const QString &title = {}, const QString &uri = {})
{
    SongEntry e;
    e.id = id;
    e.pos = pos;
    e.title = title.isEmpty() ? QStringLiteral("t%1").arg(id) : title;
    e.artist = QStringLiteral("a%1").arg(id);
    e.uri = uri.isEmpty() ? QStringLiteral("f%1.flac").arg(id) : uri;
    e.duration = 60 + id;
    return e;
}

/// Очередь из `count` трэков с идентификаторами от firstId по порядку.
QList<SongEntry> queueOf(int count, int firstId = 1)
{
    QList<SongEntry> q;
    for (int i = 0; i < count; ++i)
        q.append(song(firstId + i, i));
    return q;
}

/// Только идентификаторы очереди — так сравнение нагляднее, чем дамп структур.
QList<int> ids(const QList<SongEntry> &q)
{
    QList<int> out;
    for (const SongEntry &e : q)
        out.append(e.id);
    return out;
}

/// Локальная копия обязана сохранять инвариант «pos == номер строки».
void checkPositions(const QList<SongEntry> &q)
{
    for (int i = 0; i < q.size(); ++i)
        CHECK(q[i].pos == i);
}

/// Крутим цикл, пока предикат не станет истинным или не кончится время.
template <typename Predicate>
bool waitFor(Predicate ready, int totalMs, int stepMs = 20)
{
    for (int waited = 0; waited < totalMs; waited += stepMs) {
        if (ready())
            return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, stepMs);
    }
    return ready();
}

/// Первый попавшийся файл базы данных — чем наполнить очередь в мутационной проверке.
QString firstUri(const MpdEndpoint &endpoint)
{
    // Отдельное соединение и немедленное закрытие: дочитывать до конца всю базу незачем,
    // а недопитанный ответ вместе с handle'ом всё равно уходит.
    QString uri;
    mpd_connection *conn = openMpdConnection(endpoint, nullptr);
    if (!conn)
        return uri;

    if (mpd_send_list_all_meta(conn, "/")) {
        while (mpd_entity *entity = mpd_recv_entity(conn)) {
            const bool isSong = mpd_entity_get_type(entity) == MPD_ENTITY_TYPE_SONG;
            if (isSong && uri.isEmpty()) {
                const char *raw = mpd_song_get_uri(mpd_entity_get_song(entity));
                if (raw)
                    uri = QString::fromUtf8(raw);
            }
            mpd_entity_free(entity);
            if (!uri.isEmpty())
                break;   // нужен один файл, а не весь список
        }
    } else {
        mpd_connection_clear_error(conn);
    }

    mpd_connection_free(conn);
    return uri;
}

} // namespace

TEST_SUITE("QueueDiff") {

TEST_CASE("пустой дифф при прежней длине — это «ничего не изменилось»") {
    QList<SongEntry> queue = queueOf(4);
    const auto result = applyQueueChanges(queue, {}, 4);

    CHECK(result.ok);
    CHECK(result.kind == QueueApplyResult::Kind::None);
    CHECK(result.tagRows.isEmpty());
    CHECK(ids(queue) == ids(queueOf(4)));
}

TEST_CASE("правка тегов в середине очереди трогает ровно одну строку") {
    // Замер на MPD 0.25: правка тегов второго трэка даёт одну запись `cpos: 1` с его
    // же идентификатором — позиция не меняется. Идентификатор обязан совпадать со
    // строкой, иначе это уже перемещение, а не правка тегов.
    QList<SongEntry> queue = queueOf(5, 10);
    const QList<SongEntry> changes{song(11, 1, QStringLiteral("новое имя"))};

    const auto result = applyQueueChanges(queue, changes, 5);

    REQUIRE(result.ok);
    CHECK(result.kind == QueueApplyResult::Kind::TagsOnly);
    CHECK(result.tagRows == QList<int>{1});
    CHECK(ids(queue) == ids(queueOf(5, 10)));   // порядок и состав прежние
    CHECK(queue[1].title == QStringLiteral("новое имя"));
    CHECK(queue[0].title != queue[1].title);     // соседей дифф не касался
    checkPositions(queue);
}

TEST_CASE("удаление из середины: в диффе только сдвинувшиеся, хвостовой излишек уходит") {
    // Замер: deleteid из шести строк даёт id 4,5,6 с новыми позициями; удалённый трэк
    // в ответе не упомянут вообще.
    QList<SongEntry> queue = queueOf(6);
    const QList<SongEntry> changes{song(4, 2), song(5, 3), song(6, 4)};

    const auto result = applyQueueChanges(queue, changes, 5);

    REQUIRE(result.ok);
    CHECK(result.kind == QueueApplyResult::Kind::Structural);
    CHECK(ids(queue) == QList<int>{1, 2, 4, 5, 6});
    checkPositions(queue);
}

TEST_CASE("удаление последней строки даёт пустой дифф — очередь всё равно укорачивается") {
    // Замер: delete хвоста уменьшает playlistlength, а diff остаётся пустым.
    QList<SongEntry> queue = queueOf(6);
    const auto result = applyQueueChanges(queue, {}, 5);

    REQUIRE(result.ok);
    CHECK(result.kind == QueueApplyResult::Kind::Structural);
    CHECK(ids(queue) == QList<int>{1, 2, 3, 4, 5});
    checkPositions(queue);
}

TEST_CASE("clear: пустой дифф и нулевая длина") {
    // Замер: после clear оба запроса (plchanges и plchangesposid) отдают пустой ответ.
    QList<SongEntry> queue = queueOf(6);
    const auto result = applyQueueChanges(queue, {}, 0);

    REQUIRE(result.ok);
    CHECK(result.kind == QueueApplyResult::Kind::Structural);
    CHECK(queue.isEmpty());
}

TEST_CASE("добавление в конец: новые трэки в диффе, остальные на своих местах") {
    QList<SongEntry> queue = queueOf(3);
    const QList<SongEntry> changes{song(4, 3), song(5, 4)};

    const auto result = applyQueueChanges(queue, changes, 5);

    REQUIRE(result.ok);
    CHECK(result.kind == QueueApplyResult::Kind::Structural);
    CHECK(ids(queue) == QList<int>{1, 2, 3, 4, 5});
    checkPositions(queue);
}

TEST_CASE("перемещение через начало очереди: все сдвинутые строки приходят в диффе") {
    // moveid 1 4: id 1 уехал в конец, остальные поднялись — сервер обязан сообщить
    // позицию каждого сдвинутого трэка.
    QList<SongEntry> queue = queueOf(5);
    const QList<SongEntry> changes{song(2, 0), song(3, 1), song(4, 2), song(5, 3),
                                   song(1, 4)};

    const auto result = applyQueueChanges(queue, changes, 5);

    REQUIRE(result.ok);
    CHECK(result.kind == QueueApplyResult::Kind::Structural);
    CHECK(ids(queue) == QList<int>{2, 3, 4, 5, 1});
    checkPositions(queue);
}

TEST_CASE("версия из будущего: сервер вернул всю очередь вместо диффа") {
    // Замер: нулевая или «будущая» версия ACK не дают — приходит весь список позиций.
    // Случай, когда наша копия уже устарела и трэков стало меньше.
    QList<SongEntry> queue = queueOf(5);
    const QList<SongEntry> whole{song(1, 0), song(2, 1), song(3, 2)};

    const auto result = applyQueueChanges(queue, whole, 3);

    REQUIRE(result.ok);
    CHECK(result.kind == QueueApplyResult::Kind::Structural);
    CHECK(ids(queue) == QList<int>{1, 2, 3});
    checkPositions(queue);

    // Если же очередь просто выросла, полный список тоже сворачивается без потерь.
    QList<SongEntry> grown = queueOf(3);
    const auto grownResult = applyQueueChanges(grown, {song(1, 0), song(2, 1), song(3, 2),
                                                       song(4, 3)}, 4);
    REQUIRE(grownResult.ok);
    CHECK(ids(grown) == QList<int>{1, 2, 3, 4});
}

TEST_CASE("дифф обязан описывать разные строки: повторы и выход за длину — отказ") {
    const QList<SongEntry> dupPosition{song(7, 0), song(8, 0)};
    QList<SongEntry> queue = queueOf(3);
    CHECK_FALSE(applyQueueChanges(queue, dupPosition, 2).ok);

    QList<SongEntry> other = queueOf(3);
    CHECK_FALSE(applyQueueChanges(other, {song(7, 5)}, 3).ok);   // позиция за концом

    QList<SongEntry> third = queueOf(3);
    const QList<SongEntry> sameId{song(1, 0), song(1, 1)};
    CHECK_FALSE(applyQueueChanges(third, sameId, 3).ok);

    // Отказ не должен оставлять копию в полураспечатанном состоянии.
    CHECK(ids(queue) == ids(queueOf(3)));
    CHECK(ids(other) == ids(queueOf(3)));
    CHECK(ids(third) == ids(queueOf(3)));

    QList<SongEntry> fourth = queueOf(3);
    CHECK_FALSE(applyQueueChanges(fourth, {}, -1).ok);           // длины не существует
}

TEST_CASE("строк меньше, чем свободных мест: дифф не покрывает очередь") {
    // Наша копия из двух трэков, сервер говорит про шесть строк и принёс одну запись —
    // четыре строки нам неоткуда взять, выдумывать их нельзя.
    QList<SongEntry> queue = queueOf(2);
    const auto result = applyQueueChanges(queue, {song(9, 5)}, 6);

    CHECK_FALSE(result.ok);
    CHECK(ids(queue) == ids(queueOf(2)));   // вызывающий поедет за полным listqueue
}

TEST_CASE("копия без идентификаторов не свёртывается ни с чем") {
    QList<SongEntry> queue = queueOf(3);
    queue[1].id = -1;   // так выглядит очередь, прочитанная без songid

    CHECK_FALSE(applyQueueChanges(queue, {song(2, 1)}, 3).ok);
}

TEST_CASE("пустая локальная копия принимает полный список очереди") {
    QList<SongEntry> empty;
    const auto result = applyQueueChanges(empty, {song(1, 0), song(2, 1)}, 2);

    REQUIRE(result.ok);
    CHECK(result.kind == QueueApplyResult::Kind::Structural);
    CHECK(ids(empty) == QList<int>{1, 2});
}

} // TEST_SUITE

/*
 * Интеграционные прогонки на живом MPD, выключены по умолчанию:
 *   GMPC_QT_TEST_MPD=/home/sss/.mpd/sock ctest --test-dir build -R test-queue-diff
 * читает только status и список очереди — чужую очередь тест не трогает.
 *
 * Мутационная прогонка добавляет и удаляет трэк, поэтому требует одноразового сервера
 * и отдельного переключателя:
 *   GMPC_QT_TEST_MPD=/tmp/gmpc-scratch-mpd/sock GMPC_QT_TEST_MPD_MUTATE=1 \
 *       ctest --test-dir build -R test-queue-diff
 */
TEST_SUITE("QueueSync") {

namespace {

MpdEndpoint endpointFromEnv()
{
    MpdEndpoint e;
    e.host = qEnvironmentVariable("GMPC_QT_TEST_MPD");
    e.port = 0;
    e.timeoutMs = 5000;
    return e;
}

/// Состояние очереди глазами сервера: версия (`playlist:`) и длина.
struct ServerState {
    quint32 version = 0;
    int length = -1;
};

ServerState readServerState(mpd_connection *conn)
{
    ServerState s;
    mpd_status *status = mpd_run_status(conn);
    if (!status) {
        mpd_connection_clear_error(conn);
        return s;
    }
    s.version = static_cast<quint32>(mpd_status_get_queue_version(status));
    s.length = static_cast<int>(mpd_status_get_queue_length(status));
    mpd_status_free(status);
    return s;
}

/// То же состояние, но из отдельного соединения: сверять надо не с собой же.
ServerState serverState(const MpdEndpoint &endpoint, QString *error)
{
    ServerState state;
    mpd_connection *conn = openMpdConnection(endpoint, error);
    if (!conn)
        return state;
    state = readServerState(conn);
    mpd_connection_free(conn);
    return state;
}

} // namespace

TEST_CASE("без адреса синхронизатор молчит и не пытается подключаться") {
    QueueSync sync;
    QSignalSpy replaced(&sync, &QueueSync::queueReplaced);
    QSignalSpy failed(&sync, &QueueSync::syncFailed);

    CHECK_FALSE(sync.endpoint().isValid());
    sync.reload();
    sync.syncNow();   // без адреса задача не ставится: ошибок быть не должно
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

    CHECK(replaced.isEmpty());
    CHECK(failed.isEmpty());

    sync.reset();     // на несуществующем соединении — просто ничего
}

TEST_CASE("живой MPD: версия и длина перечитанной очереди совпадают со status") {
    const MpdEndpoint endpoint = endpointFromEnv();
    if (!endpoint.isValid()) {
        MESSAGE("GMPC_QT_TEST_MPD не задан — интеграционная проверка пропущена");
        return;
    }

    QueueSync sync;
    sync.setEndpoint(endpoint);
    QSignalSpy replaced(&sync, &QueueSync::queueReplaced);
    sync.reload();

    REQUIRE_MESSAGE(waitFor([&replaced] { return !replaced.isEmpty(); }, 15000),
                    "очередь не перечиталась с " << endpoint.host.toStdString());

    const auto songs = replaced.last().at(0).value<QList<SongEntry>>();
    const quint32 version = replaced.last().at(1).toUInt();

    QString error;
    const ServerState state = serverState(endpoint, &error);
    REQUIRE_MESSAGE(state.length >= 0, "status не прочитан: " << error.toStdString());

    CHECK(songs.size() == state.length);
    CHECK(version == state.version);
    for (int i = 0; i < songs.size(); ++i)
        CHECK(songs[i].pos == i);   // pos обязан совпадать с номером строки

    // Повторная синхронизация на неизменённой очереди не должна ничего переписывать:
    // иначе каждое событие idle стоило бы сброса модели и потери выделения.
    const int before = replaced.count();
    QSignalSpy tags(&sync, &QueueSync::tagsRefreshed);
    sync.syncNow();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1000);
    CHECK(replaced.count() == before);
    CHECK(tags.isEmpty());

    // Тот же результат без обращения к сети: синхронизатор помнит версию очереди.
    sync.syncNow();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
    CHECK(replaced.count() == before);
}

TEST_CASE("живой MPD (одноразовый): изменение очереди другим клиентом доходит само") {
    const MpdEndpoint endpoint = endpointFromEnv();
    if (!endpoint.isValid() || qEnvironmentVariable("GMPC_QT_TEST_MPD_MUTATE").isEmpty()) {
        MESSAGE("нужны GMPC_QT_TEST_MPD и GMPC_QT_TEST_MPD_MUTATE на одноразовом сервере");
        return;
    }

    mpd_connection *prober = openMpdConnection(endpoint, nullptr);
    REQUIRE(prober);
    const QString uri = firstUri(endpoint);
    REQUIRE_MESSAGE(!uri.isEmpty(), "в базе тестового MPD нет ни одного файла");

    QueueSync sync;
    sync.setEndpoint(endpoint);
    QSignalSpy replaced(&sync, &QueueSync::queueReplaced);
    sync.reload();
    REQUIRE(waitFor([&replaced] { return !replaced.isEmpty(); }, 15000));

    const int initialLength = readServerState(prober).length;

    // ── добавление: дифф обязан прийти одной записью, без перечтения очереди ──────
    REQUIRE_MESSAGE(mpd_run_add(prober, uri.toUtf8().constData()),
                    "add не прошёл — тестовый сервер недоступен для записи");
    sync.syncNow();
    const bool addDelivered = waitFor([&replaced, initialLength] {
        return !replaced.isEmpty()
            && replaced.last().at(0).value<QList<SongEntry>>().size() == initialLength + 1;
    }, 15000);
    REQUIRE_MESSAGE(addDelivered, "добавленный трэк не появился в очереди");

    const QList<SongEntry> afterAdd = replaced.last().at(0).value<QList<SongEntry>>();
    CHECK(afterAdd.size() == initialLength + 1);
    CHECK(afterAdd.last().uri == uri);   // новые строки встают на свои позиции, а не в начало

    const int addedId = afterAdd.last().id;
    REQUIRE(addedId >= 0);

    // ── удаление: сервер про удалённый трэк молчит совсем ────────────────────────
    // Тот же плановый тест «изменение очереди другим клиентом видно без переключения
    // вкладок», только в обе стороны: здесь дифф сообщает лишь сдвинувшиеся строки.
    REQUIRE(mpd_run_delete_id(prober, static_cast<unsigned>(addedId)));
    sync.syncNow();
    const bool removeDelivered = waitFor([&replaced, initialLength] {
        return !replaced.isEmpty()
            && replaced.last().at(0).value<QList<SongEntry>>().size() == initialLength;
    }, 15000);
    REQUIRE_MESSAGE(removeDelivered, "удаление трэка не отразилось в очереди");

    const QList<SongEntry> afterDelete = replaced.last().at(0).value<QList<SongEntry>>();
    CHECK(afterDelete.size() == initialLength);
    for (const SongEntry &e : afterDelete)
        CHECK(e.id != addedId);   // фантомный трэк из прореженного диффа не должен остаться

    mpd_connection_free(prober);
}

} // TEST_SUITE
