/**
 * @file queue-sync.cpp
 * @brief Worker-поток, держащий локальную копию очереди MPD.
 */

#include "queue-sync.h"

#include "core/mpd-error.h"
#include "core/queue-diff.h"
#include "tools/log.h"

#include <QMetaObject>
#include <QThread>
#include <mpd/client.h>

namespace {

/// Что делать с соединением после исполнения задачи.
enum class Outcome { Ok, KeepConnection, Fatal };

/// Биты заявок в QueueSync::pending_.
constexpr int kSyncBit = 1;
constexpr int kFullBit = 2;

/** @brief Попыток снять согласованный снимок очереди.
 *
 *  Нужен именно цикл, а не одна попытка: `status` и `listqueue` — две команды, между
 *  которыми очередь может измениться, а версия из первого status дальше определяет,
 *  какой дифф мы попросим.
 */
constexpr int kFullAttempts = 3;

} // namespace

/// Заявка worker'у: что сделать (для SetEndpoint — ещё и на каких настройках).
struct QueueSync::Job {
    Action action = Action::Sync;
    MpdEndpoint endpoint;   ///< смысл только для Action::SetEndpoint
};

class QueueSync::Worker : public QObject {
    Q_OBJECT
public:
    explicit Worker(std::shared_ptr<std::atomic<int>> pending)
        : pending_(std::move(pending)) {}

    ~Worker() override { close(); }

    /// Исполняется в потоке worker'а (через queued invokeMethod).
    void run(const Job &job)
    {
        if (job.action == Action::SetEndpoint) {
            applyEndpoint(job.endpoint);
            return;
        }
        if (job.action == Action::Reset) {
            forget();
            close();   // соединение потеряно — держать его смысла нет, откроем заново
            return;
        }

        // Бит снимается ДО первого обращения к серверу: заявка, вставшая в очередь после
        // нас, обязана его увидеть и тоже исполниться, иначе изменение, пришедшее во
        // время нашего чтения, останется непокрытым.
        const int bit = job.action == Action::Full ? kFullBit : kSyncBit;
        pending_->fetch_and(~bit, std::memory_order_acq_rel);

        QString error;
        if (!open(error)) {
            emit failed(error);
            return;
        }

        const Outcome outcome = job.action == Action::Full ? loadFull(error)
                                                          : syncOnce(error);
        if (outcome == Outcome::Fatal) {
            // Ответ не дочитан: соединением нельзя пользоваться, а локальная копия
            // могла быть изменена наполовину — доверяем только полному перечтению.
            close();
            forget();
        }

        if (!error.isEmpty()) {
            LOG_WARN("queue-sync: %s", qUtf8Printable(error));
            emit failed(error);
        }
    }

signals:
    void replaced(const QList<SongEntry> &songs, quint32 version);
    void tagsChanged(const QList<SongEntry> &rows);
    void failed(const QString &message);

private:
    /// Снимок состояния очереди из `status`.
    struct Snapshot {
        quint32 version = 0;
        int length = -1;
    };

    void close()
    {
        if (conn_) {
            mpd_connection_free(conn_);
            conn_ = nullptr;
        }
        connectError_.clear();
    }

    /** @brief Новые настройки сервера: накопленная копия очереди к ним не относится.
     *  Вызывается в потоке worker'а, поэтому никаких гонок с читаемой задачей нет. */
    void applyEndpoint(const MpdEndpoint &endpoint)
    {
        if (endpoint_ == endpoint)
            return;
        endpoint_ = endpoint;
        forget();
        close();   // handle открыт под прежний адрес
    }

    /// Локальная копия больше не описывает сервер: следующее обновление — полное.
    void forget()
    {
        queue_.clear();
        version_ = 0;
        hasState_ = false;
    }

    bool open(QString &error)
    {
        if (conn_)
            return true;
        conn_ = openMpdConnection(endpoint_, &connectError_);
        if (!conn_) {
            error = connectError_.isEmpty() ? tr("не удалось подключиться к MPD")
                                            : connectError_;
            return false;
        }
        connectError_.clear();
        return true;
    }

    /// Читает ошибку, снимает error-state и говорит, переживёт ли соединение её.
    Outcome takeError(QString &out)
    {
        const enum mpd_error code = mpd_connection_get_error(conn_);
        if (code == MPD_ERROR_SUCCESS)
            return Outcome::Ok;

        out = QString::fromUtf8(mpd_connection_get_error_message(conn_));
        if (code == MPD_ERROR_SERVER) {
            // get_server_error() валиден только для MPD_ERROR_SERVER.
            out = tr("MPD error %1: %2")
                      .arg(static_cast<int>(mpd_connection_get_server_error(conn_)))
                      .arg(out);
        }

        const MpdErrorAction action = classifyMpdError(static_cast<int>(code));
        mpd_connection_clear_error(conn_);
        return action == MpdErrorAction::KeepConnection ? Outcome::KeepConnection
                                                       : Outcome::Fatal;
    }

    /** @brief `status`: версия очереди и её длина.
     *
     *  Отдельный round-trip обязателен: ни `listqueue`, ни `plchanges` версию не
     *  сообщают, а в status она живёт как `playlist:` (замер на MPD 0.25 — отдельного
     *  поля queue_version нет).
     */
    Outcome readStatus(Snapshot &out, QString &error)
    {
        mpd_status *status = mpd_run_status(conn_);
        if (!status)
            return takeError(error);

        out.version = static_cast<quint32>(mpd_status_get_queue_version(status));
        // `playlistlength` в протоколе, accessor в libmpdclient 2.24 — queue_length;
        // имени mpd_status_get_playlist_length в библиотеке нет.
        out.length = static_cast<int>(mpd_status_get_queue_length(status));
        mpd_status_free(status);
        return Outcome::Ok;
    }

    /// Полный список очереди (`listqueue`).
    Outcome readQueue(QList<SongEntry> &out, QString &error)
    {
        out.clear();
        if (!mpd_send_list_queue_meta(conn_))
            return takeError(error);

        while (mpd_entity *entity = mpd_recv_entity(conn_)) {
            if (mpd_entity_get_type(entity) == MPD_ENTITY_TYPE_SONG)
                out.append(songEntryFromMpd(mpd_entity_get_song(entity)));
            mpd_entity_free(entity);
        }

        if (!mpd_response_finish(conn_))
            return takeError(error);
        return Outcome::Ok;
    }

    /// Дифф `plchanges <from>`: трэки с новой позицией либо изменёнными тегами.
    Outcome readChanges(quint32 from, QList<SongEntry> &out, QString &error)
    {
        out.clear();
        if (!mpd_send_queue_changes_meta(conn_, from))
            return takeError(error);

        while (mpd_song *song = mpd_recv_song(conn_)) {
            out.append(songEntryFromMpd(song));
            mpd_song_free(song);
        }

        if (!mpd_response_finish(conn_))
            return takeError(error);
        return Outcome::Ok;
    }

    /** @brief Перечитывает очередь целиком, следя за тем, чтобы снимок был цельным.
     *
     *  `status` до и после — не расточительность: сервер исполняет команды по очереди,
     *  поэтому между ними очередь может измениться, и версия из первого status тогда не
     *  имеет отношения к прочитанному списку. Расхождение видно именно по второй паре
     *  значений; стоит это ~0.2 мс против ~914 мс на само перечтение очереди из 65k строк.
     */
    Outcome loadFull(QString &error)
    {
        for (int attempt = 0; attempt < kFullAttempts; ++attempt) {
            Snapshot before;
            Outcome outcome = readStatus(before, error);
            if (outcome != Outcome::Ok)
                return outcome;

            QList<SongEntry> songs;
            outcome = readQueue(songs, error);
            if (outcome != Outcome::Ok)
                return outcome;

            Snapshot after;
            outcome = readStatus(after, error);
            if (outcome != Outcome::Ok)
                return outcome;

            if (before.version == after.version && before.length == after.length) {
                queue_ = std::move(songs);
                version_ = before.version;
                hasState_ = true;
                LOG_DEBUG("queue-sync: очередь перечитана целиком, %d строк, версия %u",
                          static_cast<int>(queue_.size()),
                          static_cast<unsigned>(version_));
                emit replaced(queue_, version_);
                return Outcome::Ok;
            }

            LOG_DEBUG("queue-sync: очередь изменилась во время перечтения (%u → %u), попытка %d",
                      static_cast<unsigned>(before.version),
                      static_cast<unsigned>(after.version), attempt + 1);
        }

        // Очередь крутят быстрее, чем мы её читаем. Состояния не оставляем: пусть
        // следующая заявка попробует снова, чем UI будет жить снимком неизвестной версии.
        error = tr("очередь меняется быстрее, чем успевает перечитаться");
        forget();
        return Outcome::KeepConnection;
    }

    /// `status`, и если версия очереди выросла — дифф `plchanges <version>`.
    Outcome syncOnce(QString &error)
    {
        if (!hasState_)
            return loadFull(error);   // перечитывать всё равно не с чем сравнивать

        Snapshot now;
        Outcome outcome = readStatus(now, error);
        if (outcome != Outcome::Ok)
            return outcome;

        if (now.version == version_ && now.length == static_cast<int>(queue_.size()))
            return Outcome::Ok;   // сервер очередь не трогал

        if (now.version < version_) {
            // Версия откатилась — сервер перезапустился: дифф от нашей версии он считает
            // «из будущего» и отдаёт всю очередь списком позиций. Тот же полный список,
            // только без внятной границы; проще перечитать самому.
            LOG_DEBUG("queue-sync: версия очереди откатилась с %u до %u — полное перечтение",
                      static_cast<unsigned>(version_), static_cast<unsigned>(now.version));
            forget();
            return loadFull(error);
        }

        QList<SongEntry> changes;
        outcome = readChanges(version_, changes, error);
        if (outcome != Outcome::Ok)
            return outcome;

        // Та же сверка снимка, что и в loadFull: длина бралась из status ДО диффа, а
        // между командами очередь может сдвинуться — тогда сливать не с чем.
        Snapshot check;
        outcome = readStatus(check, error);
        if (outcome != Outcome::Ok)
            return outcome;
        if (check.version != now.version || check.length != now.length) {
            forget();
            return loadFull(error);
        }

        const QueueApplyResult applied = applyQueueChanges(queue_, changes, now.length);
        if (!applied.ok) {
            // Дифф не описывает очередь: сервер хранит историю изменений короче, чем мы
            // её не читали. Единственный честный выход — полный список.
            LOG_DEBUG("queue-sync: дифф от версии %u не сошёлся с длиной %d — полное перечтение",
                      static_cast<unsigned>(version_), now.length);
            forget();
            return loadFull(error);
        }

        version_ = now.version;   // даже если Kind::None: сервер-то версию поднял
        switch (applied.kind) {
        case QueueApplyResult::Kind::None:
            break;
        case QueueApplyResult::Kind::TagsOnly:
            emit tagsChanged(changes);
            break;
        case QueueApplyResult::Kind::Structural:
            emit replaced(queue_, version_);
            break;
        }
        return Outcome::Ok;
    }

    std::shared_ptr<std::atomic<int>> pending_;
    mpd_connection *conn_ = nullptr;
    MpdEndpoint endpoint_;
    QString connectError_;
    QList<SongEntry> queue_;   ///< локальная копия очереди (pos == номер строки)
    quint32 version_ = 0;      ///< `playlist:` из status, на котором копия согласована
    bool hasState_ = false;
};

// ── Синхронизатор ────────────────────────────────────────────────────────────

QueueSync::QueueSync(QObject *parent)
    : QObject(parent),
      thread_(new QThread(this)),
      pending_(std::make_shared<std::atomic<int>>(0))
{
    qRegisterMetaType<SongEntry>("SongEntry");
    qRegisterMetaType<QList<SongEntry>>("QList<SongEntry>");

    worker_ = new Worker(pending_);
    worker_->moveToThread(thread_);

    // Сигналы идут из потока worker'а в наш; очередь событий Qt их рассортирует, а
    // модели в GUI останется только наложить присланное на себя.
    connect(worker_, &Worker::replaced, this, &QueueSync::queueReplaced);
    connect(worker_, &Worker::tagsChanged, this, &QueueSync::tagsRefreshed);
    connect(worker_, &Worker::failed, this, &QueueSync::syncFailed);

    thread_->start();
}

QueueSync::~QueueSync()
{
    // Ждём, пока worker дочитает текущий ответ: после wait() он гарантированно не
    // трогает ни pending_, ни наши члены.
    thread_->quit();
    if (!thread_->wait(5000))
        LOG_WARN("QueueSync: worker-поток очереди не завершился за 5 с");

    delete worker_;   // поток завершён → удалять объект из нашего потока безопасно
}

void QueueSync::setEndpoint(const MpdEndpoint &endpoint)
{
    if (endpoint_ == endpoint)
        return;
    endpoint_ = endpoint;

    // Смена адреса — тоже заявка в ту же очередь: worker поменяет настройки ровно когда
    // до неё дойдёт, а не посреди читаемой задачи.
    Job job;
    job.action = Action::SetEndpoint;
    job.endpoint = endpoint;
    Worker *worker = worker_;
    QMetaObject::invokeMethod(worker_, [worker, job] { worker->run(job); },
                              Qt::QueuedConnection);
}

void QueueSync::reload()
{
    request(Action::Full);
}

void QueueSync::syncNow()
{
    request(Action::Sync);
}

void QueueSync::request(Action action)
{
    if (!endpoint_.isValid())
        return;   // без адреса задача только породила бы ошибку подключения

    const int bit = action == Action::Full ? kFullBit : kSyncBit;
    const int pending = pending_->load(std::memory_order_acquire);
    if (action == Action::Sync) {
        // Любая ожидающая заявка прочитает сервер позже этого вызова, значит и это
        // изменение увидит: вторая задача ничего не добавила бы.
        if (pending != 0)
            return;
    } else if ((pending & kFullBit) != 0) {
        return;   // полное перечтение уже ждёт своей очереди — второй раз за 914 мс не надо
    }

    pending_->fetch_or(bit, std::memory_order_acq_rel);

    Worker *worker = worker_;
    Job job;
    job.action = action;
    QMetaObject::invokeMethod(worker_, [worker, job] { worker->run(job); },
                              Qt::QueuedConnection);
}

void QueueSync::reset()
{
    // Состояние выбрасываем без условий: версия очереди у переподключившегося сервера
    // может оказаться меньше нашей, и дифф от неё ничего не даст.
    Worker *worker = worker_;
    Job job;
    job.action = Action::Reset;
    QMetaObject::invokeMethod(worker_, [worker, job] { worker->run(job); },
                              Qt::QueuedConnection);
}

#include "queue-sync.moc"
