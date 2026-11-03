#pragma once

/**
 * @file queue-sync.h
 * @brief Зеркало очереди MPD вне GUI-потока: полная загрузка и diff по `plchanges`.
 *
 * Причина две, обе из замеров (PLAN.md, фазы 6–7):
 *  - `listqueue` у очереди из 65 279 позиций читается ~914 мс. В GUI-потоке это
 *    зависшее приложение, а на каждое событие `queue` такую перечтенку не зовёшь;
 *  - libmpdclient не потокобезопасна: одно соединение обслуживает один поток, а
 *    общее соединение Connection занято синхронными командами GUI. Поэтому у
 *    синхронизатора — собственный handle (те же настройки, что у поиска и
 *    слушателя событий).
 *
 * Дальше очередь обновляется диффом `plchanges <version>`; его семантика
 * (разреженный ответ, молчание про удаления, пустой ответ после `clear`) разобрана
 * в queue-diff.h, а само слияние — чистая функция оттуда же. Рабочее состояние —
 * копия очереди и версия — живёт в worker-потоке: в GUI уходят либо новый список
 * целиком, либо обновлённые теги строк.
 */

#include <QList>
#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

#include "core/mpd-endpoint.h"
#include "core/playlist-model.h"

class QThread;

/** @brief Синхронизатор очереди: worker-поток с собственным mpd_connection. */
class QueueSync : public QObject {
    Q_OBJECT
public:
    explicit QueueSync(QObject *parent = nullptr);
    /// Останавливает поток и закрывает его соединение.
    ~QueueSync() override;

    /** @brief Настройки сервера для собственного соединения worker'а.
     *  Смена адреса (хост или порт) сбрасывает накопленное состояние: версия
     *  очереди у другого сервера ничего не значит. */
    void setEndpoint(const MpdEndpoint &endpoint);
    [[nodiscard]] MpdEndpoint endpoint() const { return endpoint_; }

    /// Перечитать очередь целиком (`status` + `listqueue`).
    void reload();

    /** @brief Инкрементальная синхронизация: `status`, и если версия изменилась —
     *  `plchanges <version>`. Пока состояния нет, делает полную загрузку.
     *
     *  Вызывается по событию idle `queue`/`playlist` и как подстраховка по тикам
     *  статуса: лишний вызов дешёв — worker сам увидит, что версия не менялась. */
    void syncNow();

    /// Забыть накопленное состояние (соединение потеряно): следующее обновление — полное.
    void reset();

signals:
    /** @brief Очередь перечитана или перестроена по диффу: модели нужен reset.
     *  @param version версия очереди, которой соответствует список. */
    void queueReplaced(const QList<SongEntry> &songs, quint32 version);

    /** @brief Теги строк обновились, состав и порядок прежние.
     *  Модели хватит dataChanged() по этим строкам — reset сбросил бы выделение
     *  и прокрутку без всякой причины. */
    void tagsRefreshed(const QList<SongEntry> &rows);

    /// Синхронизация не удалась (сервер недоступен, ответ не разобран).
    void syncFailed(const QString &message);

private:
    class Worker;

    struct Job;

    /// Что должна сделать поставленная в очередь задача.
    enum class Action { Full, Sync, Reset, SetEndpoint };

    /** @brief Ставит задачу в очередь worker'а, если такая уже не ждёт своей очереди.
     *  @param action Full либо Sync — остальные заявки уходят напрямую как Job.
     *
     *  Задача читает состояние сервера в момент исполнения, а не в момент вызова: пока
     *  она стоит в очереди, повторный запрос ничего нового не принесёт. Без этого
     *  переподключение с пачкой idle-событий поставило бы десяток полных перечтений
     *  очереди по ~900 мс каждое.
     */
    void request(Action action);

    QThread *thread_ = nullptr;
    Worker *worker_ = nullptr;
    MpdEndpoint endpoint_;
    /// Заявки, ещё не начатые worker'ом: бит 0 — Sync, бит 1 — Full (см. request()).
    std::shared_ptr<std::atomic<int>> pending_;
};

Q_DECLARE_METATYPE(SongEntry)
Q_DECLARE_METATYPE(QList<SongEntry>)
