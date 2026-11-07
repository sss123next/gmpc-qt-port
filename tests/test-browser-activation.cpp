#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-browser-activation.cpp
 * @brief Семантика команд двойного клика (7.3): play_path, findadd, load, playlistdelete.
 *
 * Юнит-тестами это не покрывается: `MpdCommands` говорит с сервером через
 * невиртуальный libmpdclient, и mocking здесь проверял бы наши догадки о протоколе,
 * а не протокол. Поэтому кейсы смотрят на состояние очереди глазами **отдельного**
 * соединения — тот же приём, что в test-queue-diff.
 *
 * Прогоны выключены по умолчанию:
 *   GMPC_QT_TEST_MPD=/home/sss/.mpd/sock ctest --test-dir build -R test-browser-activation
 * Мутационные кейсы (`addid`/`playid`/`findadd`/`save`/`playlistdelete`) требуют
 * дополнительно GMPC_QT_TEST_MPD_MUTATE=1 и **одноразового** сервера: они играют чужую
 * очередь, дописывают в неё трэки и правят файл списка. Разворачивается он скриптом
 * /tmp/gmpc-scratch/setup.sh (свой music_directory с четырьмя заглушками FLAC, своя БД,
 * свой сокет, `audio_output { type "null" }`), собирается так:
 *   bash /tmp/gmpc-scratch/setup.sh                      # mpd + mpc update + очередь
 *   GMPC_QT_TEST_MPD=/tmp/gmpc-scratch/mpd/sock GMPC_QT_TEST_MPD_MUTATE=1 \
 *       ctest --test-dir build -R test-browser-activation
 * Две ловушки, на которые натыкаешься сразу: `port "0"` MPD 0.25 отвергает («Value must
 * be positive»), а нулевой аудиовывод выбирает буферы без ожидания реального времени —
 * трэк заканчивается раньше, чем успевашь спросить позицию (замерено: -1 сразу после
 * playid). Отсюда `PlaybackPin` ниже и ненулевой TCP-порт в конфиге.
 *
 * Прогон **не идемпотентен**: `findadd` и `load` дописывают очередь и оставляют её больше,
 * чем была (замерено: второй подряд прогон на том же сервере даёт 16 строк вместо 7). Это
 * цена проверки настоящих команд над настоящей очередью, поэтому между прогонами сервер
 * пересоздают тем же `setup.sh`, а не запускают кейсы дважды над живым сервером.
 */

#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

#include <mpd/client.h>

#include "core/connection.h"
#include "core/mpd-commands.h"
#include "core/mpd-endpoint.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QCoreApplication app(argc, &argv);

namespace {

MpdEndpoint endpointFromEnv()
{
    MpdEndpoint e;
    e.host = qEnvironmentVariable("GMPC_QT_TEST_MPD");
    e.timeoutMs = 5000;
    return e;
}

bool mutationAllowed()
{
    return !qEnvironmentVariable("GMPC_QT_TEST_MPD_MUTATE").isEmpty();
}

/** @brief Снимок очереди глазами сервера.
 *  uri и id читаются из одного ответа, чтобы строка и её идентификатор гарантированно
 *  относились друг к другу (два запроса — две разные очереди).
 */
struct QueueSnapshot {
    QStringList uris;
    QList<int> ids;

    int indexOf(const QString &uri) const { return uris.indexOf(uri); }
};

QueueSnapshot readQueue(const MpdEndpoint &endpoint)
{
    QueueSnapshot snap;
    mpd_connection *c = openMpdConnection(endpoint);
    if (!c)
        return snap;

    if (mpd_send_list_queue_meta(c)) {
        while (struct mpd_song *song = mpd_recv_song(c)) {
            snap.uris << QString::fromUtf8(mpd_song_get_uri(song));
            const unsigned id = mpd_song_get_id(song);
            // 0 — сервер не отдаёт songid; тогда идентификаторов в снимке нет вовсе.
            snap.ids << (id > 0 ? static_cast<int>(id) : -1);
            mpd_song_free(song);
        }
    }
    mpd_response_finish(c);
    mpd_connection_free(c);
    return snap;
}

/// Позиция играющего трэка в очереди глазами сервера (-1 — ничего не играет).
int playingPos(const MpdEndpoint &endpoint)
{
    mpd_connection *c = openMpdConnection(endpoint);
    if (!c)
        return -2;   // отдельный код: «не удалось спросить», а не «ничего не играет»

    int pos = -2;
    if (struct mpd_status *st = mpd_run_status(c)) {
        pos = mpd_status_get_song_pos(st);
        mpd_status_free(st);
    } else {
        mpd_connection_clear_error(c);
    }
    mpd_connection_free(c);
    return pos;
}

/** @brief Пришпиливает воспроизведение к одному трэку на время проверки.
 *  Аудио-вывод `null` выбирает буферы, не дожидаясь реального времени, поэтому
 *  заглушка в 0.2 с заканчивается раньше, чем успеваешь спросить позицию: на MPD 0.25
 *  `status` успевал отдать -1 там, где мгновение назад играл нужный трэк. `single` +
 *  `repeat` зацикливают загруженный трэк — позиция перестаёт зависеть от скорости
 *  вывода (замерено: одна и та же строка очереди держится сколько угодно опросов).
 */
class PlaybackPin {
public:
    explicit PlaybackPin(const MpdEndpoint &endpoint) : endpoint_(endpoint)
    {
        mpd_connection *c = openMpdConnection(endpoint);
        if (!c)
            return;
        if (struct mpd_status *st = mpd_run_status(c)) {
            hadRepeat_ = mpd_status_get_repeat(st);
            hadSingle_ = mpd_status_get_single(st);
            mpd_status_free(st);
        } else {
            mpd_connection_clear_error(c);
        }
        mpd_run_repeat(c, true);
        mpd_run_single(c, true);
        mpd_connection_free(c);
        armed_ = true;
    }

    ~PlaybackPin()
    {
        if (!armed_)
            return;
        mpd_connection *c = openMpdConnection(endpoint_);
        if (!c)
            return;
        mpd_run_single(c, hadSingle_);
        mpd_run_repeat(c, hadRepeat_);
        mpd_connection_free(c);
    }

    PlaybackPin(const PlaybackPin &) = delete;
    PlaybackPin &operator=(const PlaybackPin &) = delete;

private:
    MpdEndpoint endpoint_;
    bool hadRepeat_ = false;
    bool hadSingle_ = false;
    bool armed_ = false;
};

/// Артист трэка в очереди (по URI) — группировку для findadd берём из реальных тегов.
QString queuedArtist(const MpdEndpoint &endpoint, const QString &uri)
{
    QString artist;
    mpd_connection *c = openMpdConnection(endpoint);
    if (!c)
        return artist;

    if (mpd_send_list_queue_meta(c)) {
        while (struct mpd_song *song = mpd_recv_song(c)) {
            if (uri == QString::fromUtf8(mpd_song_get_uri(song))) {
                const char *a = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
                if (a)
                    artist = QString::fromUtf8(a);
                mpd_song_free(song);
                break;
            }
            mpd_song_free(song);
        }
    }
    mpd_response_finish(c);
    mpd_connection_free(c);
    return artist;
}

/** @brief Строки сохранённого списка (`listplaylist`, голые URI без метаданных). */
QStringList storedPlaylist(const MpdEndpoint &endpoint, const QString &name)
{
    QStringList uris;
    mpd_connection *c = openMpdConnection(endpoint);
    if (!c)
        return uris;

    // listplaylist читается через mpd_recv_entity: отдельного «recv_playlist_song» в
    // libmpdclient 2.24 нет, а сущность здесь всегда Song.
    if (mpd_send_list_playlist(c, name.toUtf8().constData())) {
        while (struct mpd_entity *e = mpd_recv_entity(c)) {
            if (mpd_entity_get_type(e) == MPD_ENTITY_TYPE_SONG)
                uris << QString::fromUtf8(mpd_song_get_uri(mpd_entity_get_song(e)));
            mpd_entity_free(e);
        }
    }
    mpd_response_finish(c);
    mpd_connection_free(c);
    return uris;
}

/** @brief Имена сохранённых списков глазами сервера (`listplaylists`).
 *  Нужен, чтобы отличать «список создан» от «файла не появилось»: содержимое пустого
 *  списка ничего об этом не говорит.
 */
QStringList storedPlaylistNames(const MpdEndpoint &endpoint)
{
    QStringList names;
    mpd_connection *c = openMpdConnection(endpoint);
    if (!c)
        return names;

    if (mpd_send_list_playlists(c)) {
        while (struct mpd_playlist *pl = mpd_recv_playlist(c)) {
            names << QString::fromUtf8(mpd_playlist_get_path(pl));
            mpd_playlist_free(pl);
        }
    }
    mpd_response_finish(c);
    mpd_connection_free(c);
    return names;
}

/** @brief Копилка отказов сервера за время проверки.
 *
 *  MpdCommands сам вызывает consumeError() после каждой команды (иначе соединение
 *  осталось бы в error-state до конца прогона), поэтому снаружи ошибку видно только
 *  сигналом commandFailed: повторный conn.consumeError() уже всегда пуст.
 */
class FailureLog {
public:
    explicit FailureLog(Connection &conn)
    {
        QObject::connect(&conn, &Connection::commandFailed,
                         [this](const QString &m) { messages_ << m; });
    }

    const QStringList &messages() const { return messages_; }

private:
    QStringList messages_;
};

/// Открывает соединение приложения по адресу из окружения; false — сервера нет.
bool openAppConnection(Connection *conn, const MpdEndpoint &endpoint)
{
    if (!endpoint.isValid())
        return false;
    return conn->connectToServer(endpoint.host, endpoint.port, endpoint.password,
                                 endpoint.timeoutMs);
}

const char *kSkipRead = "GMPC_QT_TEST_MPD не задан — интеграционная проверка пропущена";
const char *kSkipWrite =
    "GMPC_QT_TEST_MPD_MUTATE не задан — мутационная проверка пропущена"
    " (одноразовый сервер обязателен)";

/// Причина пропуска одним выражением: doctest разворачивает MESSAGE в цепочку `<<`,
/// и тернарный оператор внутри неё связывается иначе, чем ожидается.
const char *skipReason(const MpdEndpoint &endpoint)
{
    if (!endpoint.isValid())
        return kSkipRead;
    return mutationAllowed() ? "" : kSkipWrite;
}

} // namespace

TEST_SUITE("BrowserActivation") {

/** @brief Двойной клик по строке, которая уже стоит в очереди: играть её, не дублируя. */
TEST_CASE("активация трэка из очереди играет его и не добавляет копию")
{
    const MpdEndpoint ep = endpointFromEnv();
    if (!ep.isValid()) {
        MESSAGE(kSkipRead);
        return;
    }

    Connection conn;
    REQUIRE(openAppConnection(&conn, ep));
    MpdCommands cmds(&conn);

    // Мутация: играем чужую очередь. Отдельный переключатель и одноразовый сервер.
    if (!mutationAllowed()) {
        MESSAGE(kSkipWrite);
        return;
    }

    const QueueSnapshot before = readQueue(ep);
    if (before.uris.size() < 2) {
        MESSAGE("в очереди меньше двух строк — нечего активировать");
        return;
    }

    // Берём не первый трэк (он мог играть до теста, и тогда «позиция изменилась» ничего
    // бы не доказала) и обязательно с уникальным в очереди URI: при дубликатах команда
    // играет **первую** строку с этим файлом — это наше правило, а не случайность, так что
    // проверять позицию последней строки на грязной очереди было бы подменой тезиса.
    int target = -1;
    QString uri;
    for (int i = before.uris.size() - 1; i > 0 && target < 0; --i) {
        if (before.uris.count(before.uris.at(i)) == 1) {
            target = i;
            uri = before.uris.at(i);
        }
    }
    if (target < 0) {
        MESSAGE("каждый URI очереди дублируется — сервер нужно пересоздать"
                " (bash /tmp/gmpc-scratch/setup.sh)");
        return;
    }

    PlaybackPin pin(ep);   // иначе позиция уйдёт раньше, чем мы её спросим
    cmds.activateUri(uri);
    CHECK(conn.consumeError("activateUri").isEmpty());

    const QueueSnapshot after = readQueue(ep);
    CHECK_EQ(after.uris.size(), before.uris.size());   // play_path не добавляет копию
    CHECK(playingPos(ep) == target);
}

/** @brief Двойной клик по трэку, которого в очереди нет: addid + playid новой строки. */
TEST_CASE("активация отсутствующего трэка дописывает его и играет именно его")
{
    // Обе причины пропуска сразу: без адреса нечего спрашивать, без разрешения нельзя
    // трогать чужую очередь.
    const MpdEndpoint ep = endpointFromEnv();
    const char *whySkip = skipReason(ep);
    if (*whySkip) {
        MESSAGE(whySkip);
        return;
    }

    Connection conn;
    REQUIRE(openAppConnection(&conn, ep));
    MpdCommands cmds(&conn);

    const QueueSnapshot initial = readQueue(ep);
    if (initial.uris.isEmpty()) {
        MESSAGE("одноразовый сервер с пустой очередью: нечем пополнять");
        return;
    }

    // Берём строку из очереди и убираем её: получаем URI, которого в очереди точно нет,
    // не трогая базу данных (на живой базе «первый не совпавший» пришлось бы искать долго).
    const QString uri = initial.uris.last();
    const int id = initial.ids.last();
    if (id < 0) {
        MESSAGE("сервер не отдал songid — удаление из очереди невозможна");
        return;
    }

    cmds.removeSongIds({id});
    CHECK_EQ(readQueue(ep).uris.size(), initial.uris.size() - 1);

    // Предпосылка проверки — трэка в очереди больше нет. На грязном сервере тот же URI
    // мог остаться в другой строке (`findadd`/`load` дубликаты не пропускают), и тогда
    // утверждать про «дописывает и играет новую строку» уже нечего: кликать надо по
    // другому файлу, а сервер — пересоздать.
    if (readQueue(ep).uris.contains(uri)) {
        MESSAGE("тот же URI остался в очереди другой строкой — одноразовый сервер нужно "
                "пересоздать (bash /tmp/gmpc-scratch/setup.sh)");
        return;
    }

    PlaybackPin pin(ep);   // позиция должна дожить до второго опроса очереди
    cmds.activateUri(uri);

    const QueueSnapshot after = readQueue(ep);
    CHECK_EQ(after.uris.size(), initial.uris.size());
    // Дописан именно этот файл, и играет именно он — а не последняя позиция до добавления.
    CHECK(after.uris.last() == uri);
    CHECK(playingPos(ep) == after.uris.size() - 1);

    // Повтор тот же URI: дубликата быть не должно (ветка playid по playlistfind).
    cmds.activateUri(uri);
    const QueueSnapshot again = readQueue(ep);
    CHECK_EQ(again.uris.size(), after.uris.size());
    CHECK(playingPos(ep) == again.uris.size() - 1);
}

/** @brief Двойной клик по группе в дереве базы: findadd дописывает всех её трэков. */
TEST_CASE("findadd добавляет все трэки группы и только их")
{
    // Обе причины пропуска сразу: без адреса нечего спрашивать, без разрешения нельзя
    // трогать чужую очередь.
    const MpdEndpoint ep = endpointFromEnv();
    const char *whySkip = skipReason(ep);
    if (*whySkip) {
        MESSAGE(whySkip);
        return;
    }

    Connection conn;
    REQUIRE(openAppConnection(&conn, ep));
    MpdCommands cmds(&conn);

    const QueueSnapshot before = readQueue(ep);
    if (before.uris.isEmpty()) {
        MESSAGE("нужен хотя бы один трэк с тегом Artist — наполните одноразовый сервер");
        return;
    }
    const QString artist = queuedArtist(ep, before.uris.first());
    if (artist.isEmpty()) {
        MESSAGE("первый трэк очереди без Artist — группировку взять неоткуда");
        return;
    }

    cmds.findAddSongs({{MPD_TAG_ARTIST, artist}});
    CHECK(conn.consumeError("findadd").isEmpty());

    const QueueSnapshot after = readQueue(ep);
    // findadd именно добавляет: то, что было собрано до клика, обязано остаться.
    CHECK(after.uris.size() > before.uris.size());
    for (int i = 0; i < before.uris.size(); ++i)
        CHECK(after.uris.at(i) == before.uris.at(i));

    // Каждый добавленный трэк — из этой группы: ограничение точное, а не по подстроке.
    int appendedWithOtherArtist = 0;
    mpd_connection *c = openMpdConnection(ep);
    REQUIRE(c != nullptr);
    if (mpd_send_list_queue_range_meta(c, static_cast<unsigned>(before.uris.size()),
                                       static_cast<unsigned>(after.uris.size()))) {
        int checked = 0;
        while (struct mpd_song *song = mpd_recv_song(c)) {
            const QString a = QString::fromUtf8(mpd_song_get_tag(song, MPD_TAG_ARTIST, 0));
            if (a != artist)
                ++appendedWithOtherArtist;
            ++checked;
            mpd_song_free(song);
        }
        MESSAGE("добавлено строк: " << checked << " (группа «" << artist.toStdString()
                                    << "»), из них с другим Artist: "
                                    << appendedWithOtherArtist);
    }
    mpd_response_finish(c);
    mpd_connection_free(c);
    CHECK_EQ(appendedWithOtherArtist, 0);

    // Пустые ограничения — намеренный no-op: иначе «двойной клик по пустой строке»
    // означал бы «всю базу в очередь».
    const int lenBeforeNoop = readQueue(ep).uris.size();
    cmds.findAddSongs({});
    CHECK_EQ(readQueue(ep).uris.size(), lenBeforeNoop);
}

/** @brief Редактор плейлистов: load и playlistdelete по позиции сохранённого списка. */
TEST_CASE("сохранённый список грузится в очередь, позиция из него удаляется")
{
    // Обе причины пропуска сразу: без адреса нечего спрашивать, без разрешения нельзя
    // трогать чужую очередь.
    const MpdEndpoint ep = endpointFromEnv();
    const char *whySkip = skipReason(ep);
    if (*whySkip) {
        MESSAGE(whySkip);
        return;
    }

    Connection conn;
    REQUIRE(openAppConnection(&conn, ep));
    MpdCommands cmds(&conn);

    const QueueSnapshot queue = readQueue(ep);
    if (queue.uris.size() < 2) {
        MESSAGE("нужны минимум две строки очереди, чтобы сохранить и урезать список");
        return;
    }

    const QString name = QStringLiteral("gmpc-qt-73-tmp");
    mpd_connection *admin = openMpdConnection(ep);
    REQUIRE(admin != nullptr);
    mpd_run_rm(admin, name.toUtf8().constData());   // мусор прошлого прогона
    mpd_connection_clear_error(admin);

    cmds.saveQueueAs(name);
    CHECK(conn.consumeError("save").isEmpty());

    const QStringList stored = storedPlaylist(ep, name);
    REQUIRE_EQ(stored.size(), queue.uris.size());

    cmds.loadStoredPlaylist(name);
    CHECK(conn.consumeError("load").isEmpty());

    // Что именно делает `load` — заменяет очередь или дописывает в неё — замеряем,
    // а не предполагаем: проверяем лишь то, что список в очереди оказался.
    const QueueSnapshot loaded = readQueue(ep);
    for (const QString &uri : stored)
        CHECK(loaded.uris.contains(uri));
    MESSAGE("до load в очереди " << queue.uris.size() << ", после load — "
                                 << loaded.uris.size()
                                 << " при длине списка " << stored.size());

    // Удаление позиции меняет файл на сервере, а не очередь.
    cmds.deletePlaylistPosition(name, 0);
    CHECK(conn.consumeError("playlistdelete").isEmpty());

    const QStringList trimmed = storedPlaylist(ep, name);
    REQUIRE_EQ(trimmed.size(), stored.size() - 1);
    CHECK(trimmed.first() == stored.at(1));
    CHECK_EQ(readQueue(ep).uris.size(), loaded.uris.size());   // очередь не тронута

    mpd_run_rm(admin, name.toUtf8().constData());
    mpd_connection_clear_error(admin);
    mpd_connection_free(admin);
}

/** @brief Меню файлового браузера: каталог целиком добавляется именно командой add. */
TEST_CASE("каталог добавляется в очередь через add, а addid его отвергает")
{
    const MpdEndpoint ep = endpointFromEnv();
    const char *whySkip = skipReason(ep);
    if (*whySkip) {
        MESSAGE(whySkip);
        return;
    }

    // Каталог берём у самого сервера: выдуманный путь ничего бы не доказал.
    QString dir;
    {
        mpd_connection *c = openMpdConnection(ep);
        REQUIRE(c != nullptr);
        if (mpd_send_list_meta(c, "")) {
            while (struct mpd_entity *e = mpd_recv_entity(c)) {
                if (dir.isEmpty() && mpd_entity_get_type(e) == MPD_ENTITY_TYPE_DIRECTORY)
                    dir = QString::fromUtf8(
                        mpd_directory_get_path(mpd_entity_get_directory(e)));
                mpd_entity_free(e);
            }
        } else {
            MESSAGE("lsinfo не ответил: "
                    << mpd_connection_get_error_message(c));
            mpd_connection_clear_error(c);
        }
        mpd_response_finish(c);
        mpd_connection_free(c);
    }
    if (dir.isEmpty()) {
        MESSAGE("на сервере нет ни одного каталога — проверять нечего");
        return;
    }

    Connection conn;
    REQUIRE(openAppConnection(&conn, ep));
    MpdCommands cmds(&conn);

    const QueueSnapshot before = readQueue(ep);
    cmds.addUri(dir);
    CHECK(conn.consumeError("add").isEmpty());

    const QueueSnapshot after = readQueue(ep);
    MESSAGE("add «" << dir.toStdString() << "»: было " << before.uris.size()
                    << ", стало " << after.uris.size());
    REQUIRE(after.uris.size() > before.uris.size());

    // Добавлен именно каталог, а не строка с его именем: всё добавленное — из него.
    int foreign = 0;
    for (int i = before.uris.size(); i < after.uris.size(); ++i) {
        if (!after.uris.at(i).startsWith(dir + QLatin1Char('/')))
            ++foreign;
    }
    CHECK_EQ(foreign, 0);

    // Тот же URI через addid — отказ: у каталога нет song id. Именно этот ACK и заставляет
    // меню браузеров вести папки через `add` (замер на MPD 0.25).
    {
        mpd_connection *c = openMpdConnection(ep);
        REQUIRE(c != nullptr);
        const int id = mpd_run_add_id(c, dir.toUtf8().constData());
        CHECK_EQ(id, -1);
        const QString err = QString::fromUtf8(mpd_connection_get_error_message(c));
        MESSAGE("addid каталога: " << err.toStdString());
        CHECK(err.contains(QLatin1String("No such song")));
        mpd_connection_clear_error(c);
        mpd_connection_free(c);
    }

    // За собой убираем (deleteid по добавленным идентификаторам), иначе каждый прогон
    // удваивает очередь и следующий прогон уже не о том.
    QList<int> added;
    const int tail = qMin(after.uris.size(), after.ids.size());
    for (int i = before.uris.size(); i < tail; ++i) {
        if (after.ids.at(i) > 0)
            added << after.ids.at(i);
    }
    if (!added.isEmpty())
        cmds.removeSongIds(added);
}

/** @brief Редактор плейлистов: почему номера удаления идут убывающими. */
TEST_CASE("позиции сохранённого списка удаляются в убывающем порядке номеров")
{
    const MpdEndpoint ep = endpointFromEnv();
    const char *whySkip = skipReason(ep);
    if (*whySkip) {
        MESSAGE(whySkip);
        return;
    }

    Connection conn;
    REQUIRE(openAppConnection(&conn, ep));
    MpdCommands cmds(&conn);

    const QStringList base = readQueue(ep).uris;
    if (base.size() < 4) {
        MESSAGE("нужны минимум четыре строки очереди, чтобы сравнить два порядка");
        return;
    }

    mpd_connection *admin = openMpdConnection(ep);
    REQUIRE(admin != nullptr);
    const QString descName = QStringLiteral("gmpc-qt-74-desc");
    const QString ascName = QStringLiteral("gmpc-qt-74-asc");
    for (const QString &n : {descName, ascName}) {
        mpd_run_rm(admin, n.toUtf8().constData());     // мусор прошлого прогона
        mpd_connection_clear_error(admin);
    }

    // Ожидаемый результат удаления строк 0 и 2 — все остальные строки в своём порядке.
    QStringList expected;
    for (int i = 0; i < base.size(); ++i) {
        if (i != 0 && i != 2)
            expected << base.at(i);
    }

    // Как это делает removalRows(): сначала больший номер, потом меньший.
    cmds.saveQueueAs(descName);
    REQUIRE(conn.consumeError("save").isEmpty());
    cmds.deletePlaylistPosition(descName, 2);
    CHECK(conn.consumeError("playlistdelete").isEmpty());
    cmds.deletePlaylistPosition(descName, 0);
    CHECK(conn.consumeError("playlistdelete").isEmpty());
    const QStringList descending = storedPlaylist(ep, descName);
    CHECK(descending == expected);

    // Тот же выбор по восходящим номерам: после удаления нулевой строки бывшая третья
    // стоит под вторым номером, поэтому `playlistdelete … 2` сносит не её (замер на
    // MPD 0.25) — отсюда и отдельный порядок в removalRows().
    cmds.saveQueueAs(ascName);
    REQUIRE(conn.consumeError("save").isEmpty());
    cmds.deletePlaylistPosition(ascName, 0);
    CHECK(conn.consumeError("playlistdelete").isEmpty());
    cmds.deletePlaylistPosition(ascName, 2);
    CHECK(conn.consumeError("playlistdelete").isEmpty());
    const QStringList ascending = storedPlaylist(ep, ascName);
    CHECK(ascending != expected);

    MESSAGE("убывающий порядок оставил «" << descending.value(0).toStdString() << "…» ("
            << descending.size() << "), восходящий — «"
            << ascending.value(0).toStdString() << "…»; ожидалось «"
            << expected.value(0).toStdString() << "…»");

    for (const QString &n : {descName, ascName}) {
        mpd_run_rm(admin, n.toUtf8().constData());
        mpd_connection_clear_error(admin);
    }
    mpd_connection_free(admin);
}

/** @brief «New…» редактора на живом сервере: список появляется пустым.
 *
 *  Проверяется именно двухшаговость (save + playlistclear) — она вынужденная, и от неё
 *  зависит смысл кнопки: одной команды «создать пустой список» у MPD нет.
 */
TEST_CASE("новый сохранённый список создаётся по имени и остаётся пустым")
{
    const MpdEndpoint ep = endpointFromEnv();
    const char *whySkip = skipReason(ep);
    if (*whySkip) {
        MESSAGE(whySkip);
        return;
    }

    Connection conn;
    REQUIRE(openAppConnection(&conn, ep));
    MpdCommands cmds(&conn);
    FailureLog failures(conn);

    mpd_connection *admin = openMpdConnection(ep);
    REQUIRE(admin != nullptr);

    const QString name = QStringLiteral("gmpc-qt-75-new");
    auto dropPlaylist = [&](const QString &n) {
        mpd_run_rm(admin, n.toUtf8().constData());
        mpd_connection_clear_error(admin);
    };
    dropPlaylist(name);

    // Замер 15: `playlistclear` по имени без файла отвечает ACK No such playlist и списка
    // не создаёт — поэтому перед ним обязателен save.
    const QString missing = QStringLiteral("gmpc-qt-75-none");
    dropPlaylist(missing);
    CHECK_FALSE(mpd_run_playlist_clear(admin, missing.toUtf8().constData()));
    const QString clearErr =
        QString::fromUtf8(mpd_connection_get_error_message(admin));
    mpd_connection_clear_error(admin);
    MESSAGE("playlistclear по несуществующему имени: " << clearErr.toStdString());

    cmds.createEmptyStoredPlaylist(name);

    CHECK(failures.messages().isEmpty());
    CHECK(storedPlaylistNames(ep).contains(name));   // файл появился
    CHECK(storedPlaylist(ep, name).isEmpty());       // и он пуст

    dropPlaylist(name);
    mpd_connection_free(admin);
}

/** @brief Перемещение позиции и блока в сохранённом списке на живом сервере.
 *
 *  Тот же порядок строк, что у очереди: TO считается по списку без вынутого участка,
 *  а `to = len−count` — это «в конец», не ошибка (замер 16).
 */
TEST_CASE("перемещение блока в сохранённом списке переставляет ровно его")
{
    const MpdEndpoint ep = endpointFromEnv();
    const char *whySkip = skipReason(ep);
    if (*whySkip) {
        MESSAGE(whySkip);
        return;
    }

    Connection conn;
    REQUIRE(openAppConnection(&conn, ep));
    MpdCommands cmds(&conn);
    FailureLog failures(conn);

    mpd_connection *admin = openMpdConnection(ep);
    REQUIRE(admin != nullptr);

    const QStringList base = readQueue(ep).uris;
    if (base.size() < 4) {
        MESSAGE("нужны минимум четыре строки очереди для проверки перемещения");
        mpd_connection_free(admin);
        return;
    }

    const QString name = QStringLiteral("gmpc-qt-75-move");
    auto fillPlaylist = [&]() {
        mpd_run_rm(admin, name.toUtf8().constData());
        mpd_connection_clear_error(admin);
        for (int i = 0; i < 4; ++i)
            mpd_run_playlist_add(admin, name.toUtf8().constData(),
                                 base.at(i).toUtf8().constData());
    };

    // Строка 2 встаёт на место строки 1: TO — координата без вынутого блока.
    fillPlaylist();
    cmds.movePlaylistBlock(name, 2, 1, 1);
    CHECK(failures.messages().isEmpty());
    QStringList expected{base.at(0), base.at(2), base.at(1), base.at(3)};
    CHECK(storedPlaylist(ep, name) == expected);

    // Блок [0..2) уходит за строку 2: при движении вниз TO на длину блока меньше.
    fillPlaylist();
    cmds.movePlaylistBlock(name, 0, 2, 1);
    CHECK(failures.messages().isEmpty());
    expected = QStringList{base.at(2), base.at(0), base.at(1), base.at(3)};
    CHECK(storedPlaylist(ep, name) == expected);

    // Граница: `to = len` при count=1 сервер отвергает ACK Bad range (замер 16).
    fillPlaylist();
    const int len = storedPlaylist(ep, name).size();
    REQUIRE_EQ(len, 4);
    CHECK_FALSE(mpd_run_playlist_move(admin, name.toUtf8().constData(), 0,
                                      static_cast<unsigned>(len)));
    const QString rangeErr =
        QString::fromUtf8(mpd_connection_get_error_message(admin));
    mpd_connection_clear_error(admin);
    CHECK(rangeErr.contains(QStringLiteral("Bad range")));

    // А `to = len−count` — принимается: это «в конец».
    cmds.movePlaylistBlock(name, 0, 1, len - 1);
    CHECK(failures.messages().isEmpty());
    expected = QStringList{base.at(1), base.at(2), base.at(3), base.at(0)};
    CHECK(storedPlaylist(ep, name) == expected);

    mpd_run_rm(admin, name.toUtf8().constData());
    mpd_connection_free(admin);
}

/** @brief Удаление смежного блока позиций: одним диапазоном и ровно он.
 *
 *  Сырое `playlistdelete name start end` сервер не понимает — работает библиотечная
 *  обёртка delete_range (замер 17), поэтому проверяем обе стороны этого факта.
 */
TEST_CASE("смежный блок позиций списка удаляется одним диапазоном")
{
    const MpdEndpoint ep = endpointFromEnv();
    const char *whySkip = skipReason(ep);
    if (*whySkip) {
        MESSAGE(whySkip);
        return;
    }

    Connection conn;
    REQUIRE(openAppConnection(&conn, ep));
    MpdCommands cmds(&conn);
    FailureLog failures(conn);

    mpd_connection *admin = openMpdConnection(ep);
    REQUIRE(admin != nullptr);

    const QStringList base = readQueue(ep).uris;
    if (base.size() < 4) {
        MESSAGE("нужны минимум четыре строки очереди для проверки удаления");
        mpd_connection_free(admin);
        return;
    }

    const QString name = QStringLiteral("gmpc-qt-75-del");
    auto fillPlaylist = [&]() {
        mpd_run_rm(admin, name.toUtf8().constData());
        mpd_connection_clear_error(admin);
        for (int i = 0; i < 4; ++i)
            mpd_run_playlist_add(admin, name.toUtf8().constData(),
                                 base.at(i).toUtf8().constData());
    };

    fillPlaylist();

    // Сырая форма с двумя номерами: сервер ждёт ровно один аргумент-диапазон.
    mpd_send_command(admin, "playlistdelete", name.toUtf8().constData(), "1", "2",
                     nullptr);
    const bool rawOk = mpd_response_finish(admin);
    CHECK_FALSE(rawOk);
    const QString rawErr =
        QString::fromUtf8(mpd_connection_get_error_message(admin));
    mpd_connection_clear_error(admin);
    MESSAGE("сырое playlistdelete с диапазоном: " << rawErr.toStdString());

    // Список после неудачной сырой команды не изменился — удаляет обёртка.
    CHECK_EQ(storedPlaylist(ep, name).size(), 4);

    cmds.removePlaylistPositions(name, 1, 2);
    CHECK(failures.messages().isEmpty());
    const QStringList expected{base.at(0), base.at(3)};
    CHECK(storedPlaylist(ep, name) == expected);

    mpd_run_rm(admin, name.toUtf8().constData());
    mpd_connection_free(admin);
}

/** @brief «Add to Playlist» дописывает трэк и создаёт список на ходу (замер 18).
 *
 *  Имя другого списка сервер как путь не принимает — это и есть причина, по которой в
 *  подменю попадают только намерения с URI-путём.
 */
TEST_CASE("добавление трэка создаёт список, а имя чужого списка отвергается")
{
    const MpdEndpoint ep = endpointFromEnv();
    const char *whySkip = skipReason(ep);
    if (*whySkip) {
        MESSAGE(whySkip);
        return;
    }

    Connection conn;
    REQUIRE(openAppConnection(&conn, ep));
    MpdCommands cmds(&conn);
    FailureLog failures(conn);

    mpd_connection *admin = openMpdConnection(ep);
    REQUIRE(admin != nullptr);

    const QStringList base = readQueue(ep).uris;
    if (base.isEmpty()) {
        MESSAGE("нужна хотя бы одна строка очереди для проверки добавления");
        mpd_connection_free(admin);
        return;
    }

    const QString target = QStringLiteral("gmpc-qt-75-add");
    auto dropPlaylist = [&](const QString &n) {
        mpd_run_rm(admin, n.toUtf8().constData());
        mpd_connection_clear_error(admin);
    };
    dropPlaylist(target);

    // Файла нет — playlistadd создаёт его сам (замер 15).
    cmds.appendToStoredPlaylist(target, base.first());
    CHECK(failures.messages().isEmpty());
    CHECK(storedPlaylistNames(ep).contains(target));
    CHECK_EQ(storedPlaylist(ep, target), QStringList{base.first()});

    // Имя другого сохранённого списка вместо пути: сервер отвечает No such directory.
    const QString other = QStringLiteral("gmpc-qt-75-add-src");
    dropPlaylist(other);
    cmds.appendToStoredPlaylist(other, base.at(base.size() > 1 ? 1 : 0));
    REQUIRE(failures.messages().isEmpty());

    const int before = storedPlaylist(ep, target).size();
    cmds.appendToStoredPlaylist(target, other);

    // Отказ виден только сигналом: MpdCommands сам завершает ответ и снимает error-state.
    REQUIRE_EQ(failures.messages().size(), 1);
    const QString nameErr = failures.messages().first();
    CHECK(nameErr.contains(QStringLiteral("No such directory")));
    MESSAGE("playlistadd с именем списка вместо пути: " << nameErr.toStdString());
    CHECK_EQ(storedPlaylist(ep, target).size(), before);   // цель не изменилась

    dropPlaylist(target);
    dropPlaylist(other);
    mpd_connection_free(admin);
}

TEST_CASE("без адреса команды активации молчат и не роняют соединение")
{
    Connection conn;
    MpdCommands cmds(&conn);   // никогда не подключались: isConnected() == false

    CHECK_FALSE(conn.isConnected());
    cmds.activateUri(QStringLiteral("/nonexistent.flac"));
    cmds.findAddSongs({{MPD_TAG_ARTIST, QStringLiteral("Nobody")}});
    cmds.loadStoredPlaylist(QStringLiteral("nobody"));
    cmds.deletePlaylistPosition(QStringLiteral("nobody"), 0);
    CHECK_FALSE(conn.isConnected());
}

} // TEST_SUITE
