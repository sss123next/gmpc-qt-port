#pragma once

/**
 * @file search-engine.h
 * @brief Поиск по базе MPD вне GUI-потока: any-tag, окно, сортировка, счётчик.
 *
 * Замер на живой базе из 65 279 позиций (MPD 0.25, `build/b3_probe.c`):
 *   - `search` без окна уносит всю базу: 63 165 трэков за ~752 мс чтения;
 *   - тот же запрос с окном 0:200 — ровно 200 записей за ~158 мс;
 *   - запрос **без единого совпадения** дороже полного (~2,05 с): чтобы убедиться,
 *     что совпадений нет, сервер обходит базу целиком. Кэш не спасает — второй и
 *     третий прогон того же термина так же 2,05 с;
 *   - второй ограничитель сортировки сервер отвергает: ACK «Unknown filter type».
 *
 * Отсюда три правила: одно any-tag ограничение вместо семи AND-тегов (замер №4 —
 * семь тегов дают пустой результат), окно обязательно и ровно одна сортировка,
 * и весь запрос живёт в отдельном потоке: секундный скан в GUI-потоке недопустим.
 */

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVector>
#include <mpd/tag.h>

#include <atomic>
#include <memory>

class QThread;

/// Один трэк из результатов поиска.
struct SearchHit {
    QString uri;
    QString artist;
    QString title;
    QString album;
    QString genre;
    int duration = 0;       ///< секунды
};

/// Что ищем: поле задаёт единственный ограничитель, «везде» = any-tag.
struct SearchQuery {
    /// Поле поиска; Any соответствует mpd_search_add_any_tag_constraint().
    enum class Field { Any, Artist, AlbumArtist, Title, Album, Genre, Composer, Performer };

    static constexpr unsigned kDefaultLimit = 200;
    static constexpr unsigned kMaxLimit = 1000;

    QString term;
    Field field = Field::Any;
    unsigned offset = 0;            ///< начало окна
    unsigned limit = kDefaultLimit;
    bool sortAscending = true;

    /// Лимит в допустимых границах (0 → по умолчанию, слишком большой → потолок).
    [[nodiscard]] unsigned clampedLimit() const;
    [[nodiscard]] bool isValid() const { return !term.trimmed().isEmpty(); }
};

/// Шаг протокола, который движок передаст libmpdclient в порядке перечисления.
struct SearchStep {
    enum class Kind { AnyTag, Tag, Sort, Window };

    Kind kind = Kind::AnyTag;
    enum mpd_tag_type tag = MPD_TAG_COUNT;   ///< смысл только для Tag и Sort
    QString value;                           ///< термин ограничения
    unsigned start = 0;                      ///< окно: начало (включительно)
    unsigned end = 0;                        ///< окно: конец (исключительно)
    bool descending = false;                 ///< сортировка
};

/** @brief Собирает шаги протокола из запроса.
 *
 *  Чистая функция: вся семантика — «один any-tag вместо семи тегов», «ровно одна
 *  сортировка», «окно всегда» — проверяется тестами без соединения с MPD.
 */
[[nodiscard]] QVector<SearchStep> makeSearchSteps(const SearchQuery &query);

/** @brief Шаги для searchcount: только ограничители, без сортировки и окна.
 *
 *  `search` требует окно и допускает одну сортировку, а searchcount принимает
 *  исключительно тег-ограничители: и sort, и window сервер отвергает тем же
 *  ACK 2 «Unknown filter type» (замер build/b3_count_probe.c). Счётчик при этом
 *  считает по всей базе, что нам и нужно.
 */
[[nodiscard]] QVector<SearchStep> makeCountSteps(const SearchQuery &query);

/// Тег поля запроса; Any и неизвестные значения → MPD_TAG_COUNT.
[[nodiscard]] enum mpd_tag_type searchFieldTag(SearchQuery::Field field);

/** @brief Движок поиска: worker-поток с собственным mpd_connection.
 *
 *  libmpdclient не потокобезопасна — одно соединение обслуживает один поток,
 *  поэтому поиск ходит в MPD отдельным соединением (настройки даёт вызывающий).
 *  Ответы помечаются номером запроса: устаревшие молча выбрасываются, а worker
 *  бросает чтение на границе трэка, когда его попросили отменить.
 */
class SearchEngine : public QObject {
    Q_OBJECT
public:
    /// Настройки сервера для собственного соединения worker'а.
    struct Endpoint {
        QString host;
        int port = 0;
        QString password;
        int timeoutMs = 0;

        [[nodiscard]] bool isValid() const { return !host.isEmpty(); }
        // Сравнение нужно worker'у: при сменившихся настройках старое соединение
        // не переиспользуется.
        bool operator==(const Endpoint &o) const
        {
            return host == o.host && port == o.port && password == o.password
                && timeoutMs == o.timeoutMs;
        }
        bool operator!=(const Endpoint &o) const { return !(*this == o); }
    };

    explicit SearchEngine(QObject *parent = nullptr);
    /// Останавливает worker-поток и закрывает его соединение.
    ~SearchEngine() override;

    void setEndpoint(const Endpoint &endpoint);
    [[nodiscard]] Endpoint endpoint() const { return endpoint_; }

    /** @brief Запускает поиск, отменяя предыдущий.
     *  @return номер запроса для сопоставления с resultsReady(), либо 0, если
     *          запрос не принят (пустой термин или нет настроек сервера).
     */
    quint64 search(const SearchQuery &query);

    /// Отменяет текущий поиск: worker бросает чтение на ближайшей границе трэка.
    void cancel();

signals:
    /** @brief Поиск завершён (пустой список — тоже результат).
     *  @param requestId номер из search(); устаревшие ответы не доставляются.
     *  @param total полное число совпадений либо -1, если сервер его не сообщил.
     *  @param error текст ошибки или пустая строка.
     */
    void resultsReady(quint64 requestId, const QVector<SearchHit> &hits,
                      int total, const QString &error);

private:
    class Worker;

    /// Номер последнего принятого запроса; worker сверяется с ним на каждом трэке.
    std::shared_ptr<std::atomic<quint64>> generation_;

    QThread *thread_ = nullptr;
    Worker *worker_ = nullptr;
    Endpoint endpoint_;
};

Q_DECLARE_METATYPE(SearchHit)
Q_DECLARE_METATYPE(QVector<SearchHit>)
