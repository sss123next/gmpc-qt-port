#include "idle-watcher.h"

#include "tools/log.h"

#include <QMetaObject>
#include <QSocketNotifier>
#include <QThread>
#include <QTimer>
#include <mpd/client.h>

#include <algorithm>

namespace {

/// Подсистемы, на которые подписываемся: ровно те, что превращаем в сигналы.
constexpr unsigned kListenMask = MPD_IDLE_PLAYER | MPD_IDLE_OPTIONS | MPD_IDLE_MIXER
    | MPD_IDLE_OUTPUT | MPD_IDLE_QUEUE | MPD_IDLE_STORED_PLAYLIST
    | MPD_IDLE_DATABASE | MPD_IDLE_UPDATE;

/// Задержка переподключения, как в Connection: секунда с удвоением до получаса.
constexpr int kRetryBaseMs = 1000;
constexpr int kRetryMaxMs = 30000;

} // namespace

IdleEvents idleEventsFromMask(unsigned mask)
{
    IdleEvents events;
    if (mask & MPD_IDLE_DATABASE)         events |= IdleEvent::Database;
    if (mask & MPD_IDLE_STORED_PLAYLIST)  events |= IdleEvent::StoredPlaylist;
    if (mask & MPD_IDLE_QUEUE)            events |= IdleEvent::Queue;
    if (mask & MPD_IDLE_PLAYER)           events |= IdleEvent::Player;
    if (mask & MPD_IDLE_MIXER)            events |= IdleEvent::Mixer;
    if (mask & MPD_IDLE_OUTPUT)           events |= IdleEvent::Output;
    if (mask & MPD_IDLE_OPTIONS)          events |= IdleEvent::Options;
    if (mask & MPD_IDLE_UPDATE)           events |= IdleEvent::Update;

    // Биты sticker/message/partition/neighbor/mount отбрасываются: сообщить о них
    // UI нечем. MPD_IDLE_PLAYLIST — алиас QUEUE, отдельной ветки не требует.
    return events;
}

// ── Worker: соединение, idle-цикл и переподключение в своём потоке ───────────

class IdleWatcher::Worker : public QObject {
    Q_OBJECT
public:
    ~Worker() override { close(); }

    /** @brief Начать (или продолжить) слушать указанный сервер.
     *  Выполняется в потоке worker'а; повторный вызов с теми же настройками,
     *  когда соединение живо — no-op. */
    void begin(const MpdEndpoint &endpoint)
    {
        wanted_ = true;
        if (conn_ && endpoint_ == endpoint)
            return;

        endpoint_ = endpoint;
        retryMs_ = kRetryBaseMs;   // новая цель — считать задержку с нуля
        connectAndListen();
    }

    /// Выйти из idle и закрыть соединение.
    void end()
    {
        wanted_ = false;
        if (retryTimer_)
            retryTimer_->stop();
        close();
        setWatching(false);
    }

signals:
    void eventsReceived(IdleEvents events);
    void watchingChanged(bool watching);

private:
    /** @brief Открывает соединение и уходит в idle.
     *
     *  Вызывается только из потока worker'а — и самим Qt-циклом (через
     *  invokeMethod), и по таймеру переподключения, поэтому `QSocketNotifier`
     *  всегда создаётся там же, где крутится событийный цикл: сам он обязывает
     *  к этому, уведомление приходит в тот поток, который его создал.
     */
    void connectAndListen()
    {
        if (!wanted_)
            return;

        close();
        if (!endpoint_.isValid()) {
            // Адрес не задан: переподключаться некуда, остаётся polling.
            setWatching(false);
            return;
        }

        QString error;
        conn_ = openMpdConnection(endpoint_, &error);
        if (!conn_) {
            failAndRetry(error);
            return;
        }

        if (!mpd_send_idle_mask(conn_, static_cast<enum mpd_idle>(kListenMask))) {
            const QString text = takeError();
            close();
            failAndRetry(text);
            return;
        }

        const int fd = mpd_connection_get_fd(conn_);
        if (fd < 0) {
            close();
            failAndRetry(tr("idle: у соединения нет дескриптора сокета"));
            return;
        }

        notifier_ = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(notifier_, &QSocketNotifier::activated, this,
                [this] { ready(); });

        retryMs_ = kRetryBaseMs;
        setWatching(true);
        LOG_DEBUG("idle: слушаю события MPD на fd {}", fd);
    }

    /** @brief Сокет готов — читаем список событий и снова уходим в idle. */
    void ready()
    {
        if (!conn_)
            return;

        // Уведомление гасим на время обработки: пока мы читаем, Qt не должен
        // вызывать этот слот повторно (пока данные не появятся вновь — он и не
        // станет, но перестраховка дешёвая, а включить её нужно самим).
        if (notifier_)
            notifier_->setEnabled(false);

        // disable_timeout=true: настроенный таймаут может оказаться короче ответа,
        // и тогда обрыв в середине списка событий рвал бы соединение на пустом
        // месте. Слот вызывается только когда fd готов, а MPD пишет весь ответ
        // idle одним флашем, так что ожидание ничего не стоит.
        const enum mpd_idle mask = mpd_recv_idle(conn_, true);

        // 0 приходит и при отсутствии событий, и при ошибке — различает их
        // только get_error().
        if (mpd_connection_get_error(conn_) != MPD_ERROR_SUCCESS) {
            const QString text = takeError();
            close();
            failAndRetry(text);
            return;
        }

        if (static_cast<unsigned>(mask) != 0)
            emit eventsReceived(idleEventsFromMask(static_cast<unsigned>(mask)));

        // Ответ idle завершён: чтобы получить следующие события, надо войти в
        // idle заново. События, случившиеся между ответом и этой командой, не
        // теряются — сервер копит их для клиента, ещё не вошедшего в idle.
        if (!mpd_send_idle_mask(conn_, static_cast<enum mpd_idle>(kListenMask))) {
            const QString text = takeError();
            close();
            failAndRetry(text);
            return;
        }

        if (notifier_)
            notifier_->setEnabled(true);
    }

    /// Читает текст ошибки и снимает error-state.
    QString takeError()
    {
        if (!conn_ || mpd_connection_get_error(conn_) == MPD_ERROR_SUCCESS)
            return {};

        const enum mpd_error code = mpd_connection_get_error(conn_);
        QString text = QString::fromUtf8(mpd_connection_get_error_message(conn_));
        if (code == MPD_ERROR_SERVER) {
            // get_server_error() валиден только для MPD_ERROR_SERVER.
            text = tr("MPD error %1: %2")
                       .arg(static_cast<int>(mpd_connection_get_server_error(conn_)))
                       .arg(text);
        }
        mpd_connection_clear_error(conn_);

        // Классификация не нужна: собственных команд это соединение не шлёт, так
        // что любая ошибка — обрыв, а не отвергнутая команда. Переподключение
        // лечит и то, и другое.
        return text;
    }

    /// Сообщает о потере слушателя и ставит single-shot на новую попытку.
    void failAndRetry(const QString &error)
    {
        setWatching(false);
        if (!wanted_)
            return;   // нас уже остановили — переподключаться не надо

        LOG_WARN("idle: {} — новая попытка через {} мс", error, retryMs_);
        if (!retryTimer_) {
            retryTimer_ = new QTimer(this);
            retryTimer_->setSingleShot(true);
            connect(retryTimer_, &QTimer::timeout, this, [this] { connectAndListen(); });
        }
        retryTimer_->start(retryMs_);
        retryMs_ = std::min(retryMs_ * 2, kRetryMaxMs);
    }

    void close()
    {
        // Notifier удаляем до соединения: он ссылается на fd, который будет закрыт.
        if (notifier_) {
            delete notifier_;
            notifier_ = nullptr;
        }
        if (conn_) {
            mpd_connection_free(conn_);
            conn_ = nullptr;
        }
    }

    void setWatching(bool watching)
    {
        if (watching_ == watching)
            return;
        watching_ = watching;
        emit watchingChanged(watching);
    }

    mpd_connection *conn_ = nullptr;
    QSocketNotifier *notifier_ = nullptr;
    QTimer *retryTimer_ = nullptr;
    MpdEndpoint endpoint_;
    bool wanted_ = false;
    bool watching_ = false;
    int retryMs_ = kRetryBaseMs;
};

// ── Оболочка в GUI-потоке ────────────────────────────────────────────────────

IdleWatcher::IdleWatcher(QObject *parent)
    : QObject(parent), thread_(new QThread(this))
{
    // Сигнал с флагами летит между потоками — метатип обязателен.
    qRegisterMetaType<IdleEvents>("IdleEvents");

    worker_ = new Worker;
    worker_->moveToThread(thread_);

    // Удаляется worker только в своём потоке: у него дети (таймер переподключения,
    // notifier), а Qt рвётся останавливать таймеры из чужого потока. finished →
    // deleteLater — штатный способ сделать это до выхода из потока.
    connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);

    connect(worker_, &Worker::eventsReceived, this, [this](IdleEvents events) {
        // Один ответ idle может принести несколько подсистем — рассылаем по одной
        // сигнал на группу: подписчикам не нужно разбираться в протокольных битах.
        if (events.testFlag(IdleEvent::Player) || events.testFlag(IdleEvent::Options))
            emit playerChanged();
        if (events.testFlag(IdleEvent::Queue)
            || events.testFlag(IdleEvent::StoredPlaylist))
            emit queueChanged();
        if (events.testFlag(IdleEvent::Mixer))
            emit mixerChanged();
        // Выход включён/выключен — другим клиентом или нами: окно настроек обязано
        // перечитать список, иначе тумблер покажет состояние, которого уже нет.
        if (events.testFlag(IdleEvent::Output))
            emit outputsChanged();
        if (events.testFlag(IdleEvent::Database) || events.testFlag(IdleEvent::Update))
            emit databaseChanged();
    });

    connect(worker_, &Worker::watchingChanged, this, [this](bool watching) {
        if (watching_ == watching)
            return;
        watching_ = watching;
        emit watchingChanged(watching);
    });

    thread_->start();
}

IdleWatcher::~IdleWatcher()
{
    running_ = false;

    // Остановку просим выполнить в потоке worker'а: закрыть соединение, убрать
    // notifier и остановить таймер может только тот поток, которому они принадлежат.
    Worker *worker = worker_;
    QMetaObject::invokeMethod(worker_, [worker] {
        worker->end();
    }, Qt::QueuedConnection);

    thread_->quit();
    if (!thread_->wait(5000)) {
        // Поток завис в чтении. Удалять worker из нашего потока нельзя — он чужой,
        // — поэтому оставляем его живым: утечка одного соединения лучше UB.
        LOG_WARN("idle: worker-поток слушателя не завершился за 5 с");
        return;
    }

    // Поток завершён: finished уже поставил deleteLater, и Qt исполняет отложенные
    // удаления при выходе из потока. Сам worker здесь не трогаем.
}

void IdleWatcher::setEndpoint(const MpdEndpoint &endpoint)
{
    if (endpoint_ == endpoint)
        return;

    endpoint_ = endpoint;
    if (running_)
        start();   // переподключиться на новый адрес
}

void IdleWatcher::start()
{
    running_ = true;   // намерение слушать: смена адреса позже подхватится сама

    Worker *worker = worker_;
    const MpdEndpoint endpoint = endpoint_;
    QMetaObject::invokeMethod(worker_, [worker, endpoint] {
        worker->begin(endpoint);
    }, Qt::QueuedConnection);
}

void IdleWatcher::stop()
{
    if (!running_)
        return;
    running_ = false;

    Worker *worker = worker_;
    QMetaObject::invokeMethod(worker_, [worker] {
        worker->end();
    }, Qt::QueuedConnection);
}

#include "idle-watcher.moc"
