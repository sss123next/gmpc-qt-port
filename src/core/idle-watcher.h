#pragma once

/**
 * @file idle-watcher.h
 * @brief События MPD через `idle`: собственное соединение, свой поток, QSocketNotifier.
 *
 * Polling опрашивает статус раз в 500 мс при воспроизведении и раз в 2 с в
 * простое, значит смена состояния другим клиентом доходит до нас столько же.
 * Паттерн из документации MPD (формулировка мейнтейнера в mpd-devel):
 * `mpd_send_idle()` → fd из `mpd_connection_get_fd()` в событийный цикл → по
 * готовности `mpd_recv_idle()` → снова idle; собственная команда требует
 * `mpd_run_noidle()`. Пока клиент в idle, сервер снимает таймауты соединения.
 *
 * Почему отдельное соединение, а не общее: Connection отдаёт handle браузерам
 * через `raw()`, и там каждая команда — синхронный `mpd_run_…` из GUI-потока.
 * Вписать idle в это же соединение значит вставить noidle перед каждым вызовом
 * во всех моделях и браузерах; пропуск одного пути оставляет транспорт либо в
 * MPD_ERROR_STATE, либо в зависшем ответе. Цена собственного handle — одно
 * лишнее соединение MPD (у поиска их уже два), зато ни одной гонки с GUI-потоком
 * и никакой переделки существующих вызовов.
 */

#include <QFlags>
#include <QObject>

#include "core/mpd-endpoint.h"

class QThread;

/// Подсистемы idle; значения совпадают с битами `enum mpd_idle`.
enum class IdleEvent : unsigned {
    Database       = 0x1,   ///< база перестроена
    StoredPlaylist = 0x2,   ///< сохранённый плейлист создан/изменён/удалён/переименован
    Queue          = 0x4,   ///< очередь изменена (он же MPD_IDLE_PLAYLIST)
    Player         = 0x8,   ///< play/pause/seek/смена трэка
    Mixer          = 0x10,  ///< громкость
    Output         = 0x20,  ///< выход включён/выключен
    Options        = 0x40,  ///< random/repeat/single/consume/crossfade
    Update         = 0x80,  ///< обновление базы началось или закончилось
};

Q_DECLARE_FLAGS(IdleEvents, IdleEvent)
Q_DECLARE_OPERATORS_FOR_FLAGS(IdleEvents)

/** @brief Протокольную маску `enum mpd_idle` — в флаги.
 *  Чистая функция: разбор проверяется тестами без сервера; биты подсистем,
 *  которых мы не слушаем (и тех, что появятся позже), отбрасываются.
 */
[[nodiscard]] IdleEvents idleEventsFromMask(unsigned mask);

/** @brief Слушатель событий MPD вне GUI-потока.
 *
 *  Worker-поток держит своё соединение и `QSocketNotifier` на его fd; в idle
 *  уходит только он, поэтому команды GUI-потока не требуют noidle. Соединение
 *  живёт по mask PLAYER|OPTIONS|MIXER|QUEUE|STORED_PLAYLIST|DATABASE|UPDATE —
 *  ровно те сигналы, что здесь объявлены. При обрыве переподключается с
 *  экспоненциальной задержкой и сообщает об этом через watchingChanged(), так
 *  что вызывающий может вернуться к polling как к подстраховке.
 */
class IdleWatcher : public QObject {
    Q_OBJECT
public:
    explicit IdleWatcher(QObject *parent = nullptr);
    /// Останавливает worker-поток и закрывает его соединение.
    ~IdleWatcher() override;

    /** @brief Сменить сервер. Если слушатель запущен, переподключается сразу. */
    void setEndpoint(const MpdEndpoint &endpoint);
    [[nodiscard]] MpdEndpoint endpoint() const { return endpoint_; }

    /// Начать слушать (без валидного адреса — no-op). Повторный вызов идемпотентен.
    void start();
    /// Выйти из idle и закрыть соединение. Повторный вызов безопасен.
    void stop();

    /// true, пока соединение с событиями живо; иначе имеют смысл только тики polling.
    [[nodiscard]] bool isWatching() const { return watching_; }

signals:
    /// PLAYER или OPTIONS — состояние плеера либо random/repeat/single/consume/crossfade.
    void playerChanged();

    /// QUEUE или STORED_PLAYLIST — очередь или список сохранённых плейлистов.
    void queueChanged();

    /// MIXER — громкость изменилась другим клиентом.
    void mixerChanged();

    /// OUTPUT — выход включён или выключен (кем угодно, включая нас).
    void outputsChanged();

    /// DATABASE или UPDATE — база перестраивается или перестроена.
    void databaseChanged();

    /** @brief Слушатель жив (true) или отвалился в переподключение (false).
     *  false означает, что до следующего события полагаться можно на polling. */
    void watchingChanged(bool watching);

private:
    class Worker;

    QThread *thread_ = nullptr;
    Worker *worker_ = nullptr;
    MpdEndpoint endpoint_;
    bool running_ = false;   ///< start() вызван и не снят stop()
    bool watching_ = false;
};

Q_DECLARE_METATYPE(IdleEvents)
