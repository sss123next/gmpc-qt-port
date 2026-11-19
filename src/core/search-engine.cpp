#include "search-engine.h"

#include "core/mpd-error.h"
#include "tools/log.h"

#include <QMetaObject>
#include <QThread>
#include <mpd/client.h>

// ── Чистая часть запроса (тестируется без MPD) ───────────────────────────────

unsigned SearchQuery::clampedLimit() const
{
    if (limit == 0) return kDefaultLimit;
    return qMin(limit, kMaxLimit);
}

enum mpd_tag_type searchFieldTag(SearchQuery::Field field)
{
    switch (field) {
    case SearchQuery::Field::Artist:      return MPD_TAG_ARTIST;
    case SearchQuery::Field::AlbumArtist: return MPD_TAG_ALBUM_ARTIST;
    case SearchQuery::Field::Title:       return MPD_TAG_TITLE;
    case SearchQuery::Field::Album:       return MPD_TAG_ALBUM;
    case SearchQuery::Field::Genre:       return MPD_TAG_GENRE;
    case SearchQuery::Field::Composer:    return MPD_TAG_COMPOSER;
    case SearchQuery::Field::Performer:   return MPD_TAG_PERFORMER;
    case SearchQuery::Field::Any:         break;
    }
    return MPD_TAG_COUNT;
}

/// Единственный ограничитель запроса: any-tag либо конкретный тег.
static void appendConstraint(QVector<SearchStep> &steps, const SearchQuery &query,
                             const QString &term)
{
    // Ровно один ограничитель: any-tag покрывает все теги сразу. Семь AND-тегов
    // дают пустой результат (замер №4), а не «поиск по любому полю».
    if (query.field == SearchQuery::Field::Any) {
        steps.append({SearchStep::Kind::AnyTag, MPD_TAG_COUNT, term, 0, 0, false});
    } else {
        steps.append({SearchStep::Kind::Tag, searchFieldTag(query.field), term,
                      0, 0, false});
    }
}

QVector<SearchStep> makeSearchSteps(const SearchQuery &query)
{
    QVector<SearchStep> steps;
    if (!query.isValid())
        return steps;

    const QString term = query.term.trimmed();
    appendConstraint(steps, query, term);

    // Ровно одна сортировка: второй ограничитель сервер отвергает как
    // «Unknown filter type» (замер b3_probe.c, пункт 4).
    steps.append({SearchStep::Kind::Sort, MPD_TAG_ARTIST_SORT, QString(),
                  0, 0, !query.sortAscending});

    // Окно обязательно: без него улетает вся база (63 165 трэков за ~752 мс).
    steps.append({SearchStep::Kind::Window, MPD_TAG_COUNT, QString(),
                  query.offset, query.offset + query.clampedLimit(), false});

    return steps;
}

QVector<SearchStep> makeCountSteps(const SearchQuery &query)
{
    QVector<SearchStep> steps;
    if (!query.isValid())
        return steps;

    // Ни sort, ни window: searchcount их не понимает и отвечает ACK 2 (замер
    // b3_count_probe.c), а полное число совпадений считается и без них.
    appendConstraint(steps, query, query.term.trimmed());
    return steps;
}

// ── Worker: своё соединение и чтение ответа в своём потоке ──────────────────

/// Запрос вместе с настройками сервера на момент запуска.
struct SearchJob {
    quint64 id = 0;
    SearchQuery query;
    SearchEngine::Endpoint endpoint;
};

/// Что делать с соединением после завершения запроса.
enum class JobOutcome { Ok, KeepConnection, Fatal };

class SearchEngine::Worker : public QObject {
    Q_OBJECT
public:
    explicit Worker(std::shared_ptr<std::atomic<quint64>> generation)
        : generation_(std::move(generation)) {}

    ~Worker() override { close(); }

    /// Выполняется в потоке worker'а (вызывается через queued invokeMethod).
    void run(const SearchJob &job)
    {
        if (isStale(job.id))
            return;   // пока стояло в очереди, уже устарело

        QVector<SearchHit> hits;
        int total = -1;
        QString error;

        JobOutcome outcome = JobOutcome::Ok;
        if (!open(job.endpoint)) {
            error = connectError_;
        } else {
            outcome = execute(job, hits, total, error);
            if (outcome == JobOutcome::Fatal)
                close();   // соединением больше нельзя пользоваться — откроем заново
        }

        LOG_DEBUG("search #{}: {} трэков, всего {}{}",
                  job.id,
                  hits.size(), total,
                  error.isEmpty() ? "" : " с ошибкой");

        if (!isStale(job.id))
            emit ready(job.id, hits, total, error);
    }

signals:
    void ready(quint64 requestId, const QVector<SearchHit> &hits, int total,
               const QString &error);

private:
    [[nodiscard]] bool isStale(quint64 id) const
    {
        return id != generation_->load(std::memory_order_acquire);
    }

    void close()
    {
        if (conn_) {
            mpd_connection_free(conn_);
            conn_ = nullptr;
        }
        endpoint_ = SearchEngine::Endpoint{};
    }

    bool open(const SearchEngine::Endpoint &endpoint)
    {
        if (conn_ && endpoint_ == endpoint)
            return true;

        close();
        // Открытие общее у поиску и слушателю событий: password, проверка ошибки
        // до get_error_message(), keepalive для TCP — всё внутри openMpdConnection().
        conn_ = openMpdConnection(endpoint, &connectError_);
        if (!conn_)
            return false;

        endpoint_ = endpoint;
        connectError_.clear();
        return true;
    }

    /// Читает ошибку, снимает error-state и говорит, переживёт ли соединение её.
    JobOutcome takeError(QString &out)
    {
        const enum mpd_error code = mpd_connection_get_error(conn_);
        if (code == MPD_ERROR_SUCCESS)
            return JobOutcome::Ok;

        out = QString::fromUtf8(mpd_connection_get_error_message(conn_));
        if (code == MPD_ERROR_SERVER) {
            // get_server_error() валиден только для MPD_ERROR_SERVER.
            out = tr("MPD error %1: %2")
                      .arg(static_cast<int>(mpd_connection_get_server_error(conn_)))
                      .arg(out);
        }

        const MpdErrorAction action = classifyMpdError(static_cast<int>(code));
        mpd_connection_clear_error(conn_);
        return action == MpdErrorAction::KeepConnection ? JobOutcome::KeepConnection
                                                       : JobOutcome::Fatal;
    }

    bool applySteps(const QVector<SearchStep> &steps) const
    {
        for (const SearchStep &step : steps) {
            bool ok = false;
            switch (step.kind) {
            case SearchStep::Kind::AnyTag:
                ok = mpd_search_add_any_tag_constraint(
                    conn_, MPD_OPERATOR_DEFAULT, step.value.toUtf8().constData());
                break;
            case SearchStep::Kind::Tag:
                ok = mpd_search_add_tag_constraint(conn_, MPD_OPERATOR_DEFAULT,
                                                   step.tag,
                                                   step.value.toUtf8().constData());
                break;
            case SearchStep::Kind::Sort:
                ok = mpd_search_add_sort_tag(conn_, step.tag, step.descending);
                break;
            case SearchStep::Kind::Window:
                ok = mpd_search_add_window(conn_, step.start, step.end);
                break;
            }
            if (!ok) {
                mpd_search_cancel(conn_);   // черновик запроса, error-state не остаётся
                return false;
            }
        }
        return true;
    }

    JobOutcome execute(const SearchJob &job, QVector<SearchHit> &hits, int &total,
                       QString &error)
    {
        const QVector<SearchStep> steps = makeSearchSteps(job.query);
        if (steps.isEmpty()) {
            error = tr("поиск: пустой запрос");
            return JobOutcome::Ok;   // соединение не тронуто
        }

        if (!mpd_search_db_songs(conn_, false) || !applySteps(steps)
            || !mpd_search_commit(conn_)) {
            return takeError(error);
        }

        const unsigned limit = job.query.clampedLimit();
        while (mpd_song *song = mpd_recv_song(conn_)) {
            if (isStale(job.id)) {
                // Брошенный недопитанный ответ leaves MPD_ERROR_STATE, который не
                // лечится ничем — соединение закрываем.
                mpd_song_free(song);
                error.clear();
                return JobOutcome::Fatal;
            }

            SearchHit hit;
            const char *uri = mpd_song_get_uri(song);
            hit.uri = QString::fromUtf8(uri ? uri : "");
            hit.artist = QString::fromUtf8(mpd_song_get_tag(song, MPD_TAG_ARTIST, 0));
            hit.title = QString::fromUtf8(mpd_song_get_tag(song, MPD_TAG_TITLE, 0));
            hit.album = QString::fromUtf8(mpd_song_get_tag(song, MPD_TAG_ALBUM, 0));
            hit.genre = QString::fromUtf8(mpd_song_get_tag(song, MPD_TAG_GENRE, 0));
            hit.duration = static_cast<int>(mpd_song_get_duration(song));
            hits.append(hit);

            mpd_song_free(song);

            if (hits.size() >= static_cast<int>(limit)) {
                // Окно и так ограничивает сервер; выход раньше — страховка на случай,
                // когда server ignore window. Оставшееся дочитает response_finish().
                break;
            }
        }

        if (!mpd_response_finish(conn_))
            return takeError(error);

        if (isStale(job.id))
            return JobOutcome::Ok;   // ответ прочитан целиком, результат просто не нужен

        countTotal(job, steps, hits, total);
        return JobOutcome::Ok;
    }

    /** @brief Полное число совпадений через searchcount.
     *
     *  Второй round-trip (~156 мс на живой базе) делаем только когда окно заполнено
     *  целиком: иначе прочитанного окна уже достаточно, а пустой запрос и так стоит
     *  серверу полный обход базы. Ограничители для счётчика собираются отдельно —
     *  sort и window он не принимает (замер b3_count_probe.c).
     */
    void countTotal(const SearchJob &job, const QVector<SearchStep> &steps,
                    const QVector<SearchHit> &hits, int &total)
    {
        total = hits.size();
        if (static_cast<unsigned>(hits.size()) < steps_limit(steps))
            return;   // окно не заполнено — это и есть все совпадения

        total = -1;
        const QVector<SearchStep> countSteps = makeCountSteps(job.query);
        const bool started = mpd_searchcount_db_songs(conn_);
        if (!started || !applySteps(countSteps) || !mpd_search_commit(conn_)) {
            countError();
            return;
        }

        // Каждую пару обязан вернуть: mpd_recv_pair() без mpd_return_pair() на втором
        // же вызове роняет библиотеку ассертом PAIR_STATE_FLOATING (проверено).
        while (const mpd_pair *pair = mpd_recv_pair(conn_)) {
            if (qstrcmp(pair->name, "songs") == 0) {
                bool ok = false;
                const int n = QString::fromLatin1(pair->value).toInt(&ok);
                if (ok) total = n;
            }
            // return_pair принимает не-const указатель — штатный идиоматический cast.
            mpd_return_pair(conn_, const_cast<mpd_pair *>(pair));
        }

        // Счётчик — украшение: ради него не жалко потерять и соединение, но уже
        // прочитанные трэки остаются в силе.
        if (!mpd_response_finish(conn_)) {
            countError();
            total = -1;
        }
    }

    /// Снимает error-state после неудачи счётчика, не порождая сообщений.
    void countError()
    {
        if (mpd_connection_get_error(conn_) == MPD_ERROR_SUCCESS)
            return;
        const MpdErrorAction action =
            classifyMpdError(static_cast<int>(mpd_connection_get_error(conn_)));
        mpd_connection_clear_error(conn_);
        if (action != MpdErrorAction::KeepConnection)
            close();
    }

    static unsigned steps_limit(const QVector<SearchStep> &steps)
    {
        for (const SearchStep &step : steps) {
            if (step.kind == SearchStep::Kind::Window)
                return step.end - step.start;
        }
        return 0;
    }

    std::shared_ptr<std::atomic<quint64>> generation_;
    mpd_connection *conn_ = nullptr;
    SearchEngine::Endpoint endpoint_;
    QString connectError_;
};

// ── Движок ───────────────────────────────────────────────────────────────────

SearchEngine::SearchEngine(QObject *parent)
    : QObject(parent), thread_(new QThread(this))
{
    qRegisterMetaType<SearchHit>("SearchHit");
    qRegisterMetaType<QVector<SearchHit>>("QVector<SearchHit>");

    generation_ = std::make_shared<std::atomic<quint64>>(0);
    worker_ = new Worker(generation_);
    worker_->moveToThread(thread_);

    connect(worker_, &Worker::ready, this,
            [this](quint64 id, const QVector<SearchHit> &hits, int total,
                   const QString &error) {
        // Ответ мог встать в очередь до отмены — UI про устаревшее не узнаёт.
        if (id != generation_->load(std::memory_order_acquire)) return;
        emit resultsReady(id, hits, total, error);
    });

    thread_->start();
}

SearchEngine::~SearchEngine()
{
    // Отменяем всё живое и ждём, пока worker дочитает/бросит текущий ответ:
    // после wait() он гарантированно не трогает наши члены.
    generation_->fetch_add(1, std::memory_order_acq_rel);
    thread_->quit();
    if (!thread_->wait(5000))
        LOG_WARN("SearchEngine: worker-поток поиска не завершился за 5 с");

    // Поток завершён → удалять объект из нашего потока безопасно.
    delete worker_;
}

void SearchEngine::setEndpoint(const Endpoint &endpoint)
{
    if (endpoint_ == endpoint)
        return;

    // Любая смена настроек делает текущий ответ чужим: worker построит соединение
    // под новые, а старый результат в UI не должен уйти.
    const bool changedServer = !endpoint_.isValid() || endpoint_.host != endpoint.host
        || endpoint_.port != endpoint.port;
    endpoint_ = endpoint;
    if (changedServer)
        cancel();
}

quint64 SearchEngine::search(const SearchQuery &query)
{
    if (!query.isValid() || !endpoint_.isValid())
        return 0;

    const quint64 id = generation_->fetch_add(1, std::memory_order_acq_rel) + 1;
    Worker *worker = worker_;
    const SearchJob job{id, query, endpoint_};
    QMetaObject::invokeMethod(worker_, [worker, job] {
        worker->run(job);
    }, Qt::QueuedConnection);
    return id;
}

void SearchEngine::cancel()
{
    generation_->fetch_add(1, std::memory_order_acq_rel);
}

#include "search-engine.moc"
