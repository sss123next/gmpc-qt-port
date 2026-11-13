#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-idle-watcher.cpp
 * @brief Разбор протокольной маски idle и жизненный цикл слушателя событий.
 *
 * Соответствие битов `enum mpd_idle` флагам IdleEvent — чистая функция, она и
 * проверяется без сервера; то же касается поведения при пустом адресе и при
 * заведомо мёртвом сокете (слушатель не должен ни весеть, ни падать, ни
 * выдавать себя за живого). Доставка событий — только на живом MPD: событие
 * генерирует второе соединение, иначе проверять нечего.
 */

#include "core/idle-watcher.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <mpd/client.h>

// QSignalSpy::wait крутит событийный цикл — без приложения он бы не дождался.
static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QCoreApplication app(argc, &argv);

namespace {

/// Заведомо мёртвый адрес: connect() падает сразу, без ожидания таймаута.
MpdEndpoint deadEndpoint()
{
    MpdEndpoint e;
    e.host = "/tmp/gmpc-qt-no-such-sock";
    e.port = 0;
    e.timeoutMs = 1000;
    return e;
}

bool has(IdleEvents events, IdleEvent event)
{
    return events.testFlag(event);
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

} // namespace

TEST_SUITE("IdleWatcher") {

TEST_CASE("idleEventsFromMask maps protocol bits to flags") {
    CHECK(idleEventsFromMask(MPD_IDLE_PLAYER) == IdleEvents(IdleEvent::Player));
    CHECK(has(idleEventsFromMask(MPD_IDLE_MIXER), IdleEvent::Mixer));
    CHECK(has(idleEventsFromMask(MPD_IDLE_DATABASE), IdleEvent::Database));
    CHECK(has(idleEventsFromMask(MPD_IDLE_UPDATE), IdleEvent::Update));
    CHECK(has(idleEventsFromMask(MPD_IDLE_OPTIONS), IdleEvent::Options));
    CHECK(has(idleEventsFromMask(MPD_IDLE_STORED_PLAYLIST), IdleEvent::StoredPlaylist));
    CHECK(has(idleEventsFromMask(MPD_IDLE_OUTPUT), IdleEvent::Output));

    const IdleEvents several = idleEventsFromMask(MPD_IDLE_PLAYER | MPD_IDLE_MIXER
                                                  | MPD_IDLE_QUEUE);
    CHECK(has(several, IdleEvent::Player));
    CHECK(has(several, IdleEvent::Mixer));
    CHECK(has(several, IdleEvent::Queue));
    CHECK_FALSE(has(several, IdleEvent::Options));
}

TEST_CASE("idleEventsFromMask treats PLAYLIST as QUEUE and drops unknown bits") {
    // MPD_IDLE_PLAYLIST — алиас MPD_IDLE_QUEUE: отдельной подсистемы нет.
    CHECK(idleEventsFromMask(MPD_IDLE_PLAYLIST) == IdleEvents(IdleEvent::Queue));

    // Пустой ответ idle (например, noidle без событий) — пустые флаги.
    CHECK(idleEventsFromMask(0).toInt() == 0);

    // Подсистемы вне нашего диапазона отбрасываются: сообщить о них UI нечем,
    // и ложных сигналов они порождать не должны.
    const unsigned foreign = MPD_IDLE_STICKER | MPD_IDLE_MESSAGE
        | MPD_IDLE_SUBSCRIPTION | MPD_IDLE_PARTITION | MPD_IDLE_NEIGHBOR
        | MPD_IDLE_MOUNT;
    CHECK(idleEventsFromMask(foreign).toInt() == 0);

    // Тот же ответ вместе с известной подсистемой — только она и остаётся.
    const IdleEvents mixed = idleEventsFromMask(MPD_IDLE_PLAYER | MPD_IDLE_STICKER);
    CHECK(has(mixed, IdleEvent::Player));
    CHECK(mixed.toInt() == static_cast<unsigned>(IdleEvent::Player));
}

TEST_CASE("listener without an address stays silent and harmless") {
    IdleWatcher idle;
    QSignalSpy watching(&idle, &IdleWatcher::watchingChanged);

    // Пустой адрес — не адрес: слушать нечего, переподключение не крутится.
    CHECK_FALSE(idle.endpoint().isValid());
    idle.start();
    idle.stop();
    idle.stop();   // повторная остановка безопасна

    // Дать worker'у отработать постановку задачи: за 200 мсек он бы уже успел
    // сообщить о жизни, не будь адрес пустым.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    CHECK_FALSE(idle.isWatching());
    CHECK(watching.isEmpty());
}

TEST_CASE("listener on a dead socket reports no events and stops instantly") {
    IdleWatcher idle;

    const MpdEndpoint endpoint = deadEndpoint();
    idle.setEndpoint(endpoint);
    CHECK(idle.endpoint() == endpoint);

    idle.start();
    // Worker успевает попытаться подключиться: неудача не делает его «живым».
    waitFor([&idle] { return idle.isWatching(); }, 500);
    CHECK_FALSE(idle.isWatching());

    idle.stop();
}

TEST_CASE("changing the endpoint while stopped does not start listening") {
    IdleWatcher idle;
    QSignalSpy watching(&idle, &IdleWatcher::watchingChanged);

    MpdEndpoint first = deadEndpoint();
    idle.setEndpoint(first);
    MpdEndpoint second = deadEndpoint();
    second.host = "/tmp/gmpc-qt-other-sock";
    idle.setEndpoint(second);
    idle.setEndpoint(second);   // тот же адрес повторно — no-op

    waitFor([&idle] { return idle.isWatching(); }, 300);
    CHECK(idle.endpoint() == second);
    CHECK_FALSE(idle.isWatching());
    CHECK(watching.isEmpty());
}

/*
 * Интеграционная прогонка на живом MPD: событие генерирует второе соединение,
 * а слушатель обязан его принести. Выключена по умолчанию (CI без сервера),
 * включается так:
 *   GMPC_QT_TEST_MPD=/home/sss/.mpd/sock ctest --test-dir build -R test-idle-watcher
 */
TEST_CASE("live MPD: idle delivers a change made by another client") {
    const QString socket = qEnvironmentVariable("GMPC_QT_TEST_MPD");
    if (socket.isEmpty()) {
        MESSAGE("GMPC_QT_TEST_MPD не задан — интеграционная проверка пропущена");
        return;
    }

    IdleWatcher idle;
    MpdEndpoint endpoint;
    endpoint.host = socket;
    endpoint.timeoutMs = 5000;
    idle.setEndpoint(endpoint);

    QSignalSpy watching(&idle, &IdleWatcher::watchingChanged);
    idle.start();

    // Слушатель поднимается асинхронно: без этого проверять бессмысленно. Заодно
    // watchingChanged(true) — единственное свидетельство, что idle действительно
    // отправлен и fd стоит в notifier'е.
    REQUIRE_MESSAGE(waitFor([&idle] { return idle.isWatching(); }, 5000),
                    "слушатель не смог подключиться к " << socket.toStdString());

    QSignalSpy player(&idle, &IdleWatcher::playerChanged);

    // Генератор события — отдельное соединение и команда, которую слушатель сам
    // не шлёт: переключение random даёт подсистему OPTIONS.
    QString error;
    mpd_connection *prober = openMpdConnection(endpoint, &error);
    REQUIRE_MESSAGE(prober, error.toStdString());

    int originalRandom = -1;
    if (mpd_status *status = mpd_run_status(prober)) {
        originalRandom = mpd_status_get_random(status);
        mpd_status_free(status);
    } else {
        mpd_connection_clear_error(prober);
    }
    REQUIRE(originalRandom >= 0);

    // Замер задержки: ровно то, ради чего затевался idle. Polling в простое дал бы
    // до 2000 мс (и до 10000 мс, пока слушатель жив).
    QElapsedTimer latency;
    latency.start();
    REQUIRE(mpd_run_random(prober, originalRandom == 0));

    // Событие приходит без нашего тика; задержка idle — миллисекунды, так что
    // несколько секунд ожидания означают лишь одно: цикл не работает.
    const bool delivered = waitFor([&player] { return !player.isEmpty(); }, 5000);
    CHECK(delivered);
    if (delivered) {
        MESSAGE("задержка доставки события idle: " << latency.elapsed() << " мс");
    }

    // Настройку возвращаем: чужой плеер не должен остаться в изменённом режиме.
    mpd_run_random(prober, originalRandom != 0);
    mpd_connection_free(prober);

    idle.stop();
    CHECK(waitFor([&idle] { return !idle.isWatching(); }, 2000));
}

TEST_CASE("live MPD: включение выхода другим клиентом даёт outputsChanged") {
    const QString socket = qEnvironmentVariable("GMPC_QT_TEST_MPD");
    if (socket.isEmpty()) {
        MESSAGE("GMPC_QT_TEST_MPD не задан — интеграционная проверка пропущена");
        return;
    }

    IdleWatcher idle;
    MpdEndpoint endpoint;
    endpoint.host = socket;
    endpoint.timeoutMs = 5000;
    idle.setEndpoint(endpoint);
    idle.start();
    REQUIRE_MESSAGE(waitFor([&idle] { return idle.isWatching(); }, 5000),
                    "слушатель не смог подключиться к " << socket.toStdString());

    QSignalSpy outputs(&idle, &IdleWatcher::outputsChanged);

    QString error;
    mpd_connection *prober = openMpdConnection(endpoint, &error);
    REQUIRE_MESSAGE(prober, error.toStdString());

    // Первое устройство служебного сервера (в setup-скрипте это «null output»).
    unsigned id = 0;
    int wasEnabled = -1;
    bool found = false;   // у первого выхода может быть и id 0 — нужен отдельный признак
    if (!mpd_send_outputs(prober)) {
        mpd_connection_free(prober);
        FAIL("сервер не отдал список выходов");
        return;
    }
    while (struct mpd_output *dev = mpd_recv_output(prober)) {
        id = static_cast<unsigned>(mpd_output_get_id(dev));
        wasEnabled = mpd_output_get_enabled(dev);
        found = true;
        mpd_output_free(dev);
        break;   // одного устройства достаточно
    }
    REQUIRE_MESSAGE(found, "у сервера нет выходных устройств");

    // Тумблер дёргает чужой клиент — слушатель обязан это заметить: подсистема OUTPUT
    // раньше разбиралась и молча выбрасывалась.
    REQUIRE(mpd_run_disable_output(prober, id));
    const bool delivered = waitFor([&outputs] { return !outputs.isEmpty(); }, 5000);
    CHECK(delivered);

    if (wasEnabled == 1)
        mpd_run_enable_output(prober, id);   // чужой сервер возвращаем как был
    else
        mpd_run_disable_output(prober, id);
    mpd_connection_free(prober);

    idle.stop();
}

} // TEST_SUITE
