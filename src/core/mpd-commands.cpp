/**
 * @file mpd-commands.cpp
 * @brief Implementation of MPD player-command wrappers.
 *
 * Each function uses the CHECK prelude (returns silently if not connected)
 * and the MPD macro (expands to conn_->raw() → mpd_connection*).  RUN wraps the
 * call with Connection::consumeError(): libmpdclient leaves the connection in
 * error state after any rejected command, so without it one failed command
 * wedges every later one.
 */

#include "mpd-commands.h"
#include "tools/log.h"

MpdCommands::MpdCommands(Connection *conn, QObject *parent)
    : QObject(parent), conn_(conn)
{}

// -------------------------------------------------------------------
//  Internal helpers (macros)
// -------------------------------------------------------------------

/// Guard: return silently if the underlying MPD connection is gone.
#define CHECK do { if (!conn_->isConnected()) return; } while(0)

/// Shorthand: expand to the raw mpd_connection* pointer.
#define MPD conn_->raw()

/// Выполняет mpd_run_… и сразу разбирается с error-state (см. consumeError()).
#define RUN(what, ...) \
    do { __VA_ARGS__; conn_->consumeError(what); } while(0)

// -------------------------------------------------------------------
//  Transport
// -------------------------------------------------------------------

void MpdCommands::play()            { CHECK; RUN("play", mpd_run_play(MPD)); }
void MpdCommands::pause()           { CHECK; RUN("pause", mpd_run_pause(MPD, true)); }
void MpdCommands::togglePause() {
    CHECK;
    auto st = conn_->fetchStatus();
    RUN("pause", mpd_run_pause(MPD, st.state != MPD_STATE_PAUSE));
}
void MpdCommands::stop()            { CHECK; RUN("stop", mpd_run_stop(MPD)); }
void MpdCommands::next()            { CHECK; RUN("next", mpd_run_next(MPD)); }
void MpdCommands::previous()        { CHECK; RUN("previous", mpd_run_previous(MPD)); }

// -------------------------------------------------------------------
//  Seeking
// -------------------------------------------------------------------

void MpdCommands::seek(int s) {
    // Third argument of mpd_run_seek_current() is "relative", not "absolute".
    CHECK; RUN("seekcur", mpd_run_seek_current(MPD, s, false));
}

void MpdCommands::seekRelative(int d) {
    CHECK; RUN("seekcur", mpd_run_seek_current(MPD, d, true));
}

// -------------------------------------------------------------------
//  Volume
// -------------------------------------------------------------------

void MpdCommands::setVolume(int v)  { CHECK; RUN("setvol", mpd_run_set_volume(MPD, v)); }
void MpdCommands::volumeUp(int s)   { CHECK; auto st = conn_->fetchStatus(); RUN("setvol", mpd_run_set_volume(MPD, st.volume + s)); }
void MpdCommands::volumeDown(int s) { CHECK; auto st = conn_->fetchStatus(); RUN("setvol", mpd_run_set_volume(MPD, st.volume - s)); }

// -------------------------------------------------------------------
//  Queue manipulation
// -------------------------------------------------------------------

void MpdCommands::addUri(const QString &uri) {
    CHECK;
    const QByteArray u = uri.toUtf8();
    RUN("add", mpd_run_add(MPD, u.constData()));
}

int MpdCommands::queueSongIdForUri(const QString &uri) const {
    if (!conn_->isConnected())
        return -1;

    const QByteArray u = uri.toUtf8();
    Connection::ResponseScope scope(*conn_, "playlistfind");

    // Ограничение по URI: в enum mpd_tag_type тега FILENAME нет, для него в
    // libmpdclient отдельный вызов — он ставит фильтр `filename "…"`. Совпадение точное
    // и регистрозависимое (замерено на MPD 0.25: очередь из «a1.flac» не отвечает ни на
    // «a1», ни на «1.flac», ни на «A1.FLAC»), поэтому подстрока не годится — «Karcev-Itak»
    // не матчит «Karcev-Itak_remix».
    if (!mpd_search_queue_songs(MPD, true))
        return -1;

    bool ok = mpd_search_add_uri_constraint(MPD, MPD_OPERATOR_DEFAULT, u.constData());
    ok = ok && mpd_search_commit(MPD);
    if (!ok) {
        mpd_search_cancel(MPD);
        return -1;
    }

    int id = -1;
    while (struct mpd_song *song = mpd_recv_song(MPD)) {
        const unsigned songId = mpd_song_get_id(song);
        mpd_song_free(song);
        // Один и тот же файл может стоять в очереди несколько раз — играем первый.
        if (id < 0 && songId > 0)
            id = static_cast<int>(songId);
    }
    return id;   // ResponseScope завершит ответ и снимет error-state
}

std::optional<SongEntry> MpdCommands::songForUri(const QString &uri) const {
    if (!conn_->isConnected() || uri.isEmpty())
        return std::nullopt;

    const QByteArray u = uri.toUtf8();
    Connection::ResponseScope scope(*conn_, "search");

    // По базе, а не по очереди: строку выбирают и в дереве БД/файлов, где трэк может
    // вовсе не стоять в очередь. Ограничение точное (как filename-фильтр, замер 10).
    if (!mpd_search_db_songs(MPD, true))
        return std::nullopt;

    bool ok = mpd_search_add_uri_constraint(MPD, MPD_OPERATOR_DEFAULT, u.constData());
    ok = ok && mpd_search_commit(MPD);
    if (!ok) {
        mpd_search_cancel(MPD);
        return std::nullopt;
    }

    std::optional<SongEntry> found;
    // Ответ вычитывается до конца: брошенный список оставил бы соединение в error-state.
    while (struct mpd_song *song = mpd_recv_song(MPD)) {
        if (!found)
            found = songEntryFromMpd(song);
        mpd_song_free(song);
    }
    return found;   // ResponseScope завершит ответ и снимет error-state
}

void MpdCommands::activateUri(const QString &uri) {
    CHECK;
    if (uri.isEmpty())
        return;

    const int existing = queueSongIdForUri(uri);
    if (existing >= 0) {
        RUN("playid", mpd_run_play_id(MPD, static_cast<unsigned>(existing)));
        return;
    }

    // addid отдаёт идентификатор новой строки (0 — ошибка): играть надо именно её,
    // а не последнюю позицию, которая могла сдвинуться чужим клиентом.
    const QByteArray u = uri.toUtf8();
    const unsigned added = mpd_run_add_id(MPD, u.constData());
    conn_->consumeError("addid");
    if (added > 0)
        RUN("playid", mpd_run_play_id(MPD, added));
}

void MpdCommands::playPos(int pos) {
    CHECK;
    RUN("playid", mpd_run_play_pos(MPD, static_cast<unsigned>(pos)));
}

void MpdCommands::clearQueue() {
    CHECK;
    RUN("clear", mpd_run_clear(MPD));
}

void MpdCommands::removeSongIds(const QList<int> &ids) {
    CHECK;
    // deleteid принимает ровно один идентификатор (в libmpdclient 2.24 нет формы
    // со списком id), поэтому пачка идёт по одному: на unix-сокете это доли
    // миллисекунды на трек, а id устойчив к сдвигам очереди.
    for (int id : ids) {
        if (id < 0)
            continue;
        RUN("deleteid", mpd_run_delete_id(MPD, static_cast<unsigned>(id)));
    }
}

void MpdCommands::moveQueueBlock(int fromPos, int count, int toPos) {
    CHECK;
    if (fromPos < 0 || count <= 0 || toPos < 0)
        return;
    RUN("move_range", mpd_run_move_range(MPD, static_cast<unsigned>(fromPos),
                                         static_cast<unsigned>(fromPos + count - 1),
                                         static_cast<unsigned>(toPos)));
}

void MpdCommands::saveQueueAs(const QString &name) {
    CHECK;
    const QByteArray n = name.toUtf8();
    if (n.isEmpty())
        return;
    RUN("save", mpd_run_save(MPD, n.constData()));
}

// -------------------------------------------------------------------
//  Stored playlists / search-based queue additions
// -------------------------------------------------------------------

void MpdCommands::findAddSongs(const QVector<MpdTagConstraint> &constraints) {
    CHECK;
    if (constraints.isEmpty())
        return;   // findadd без фильтра — это «вся база», а не «ничего»

    Connection::ResponseScope scope(*conn_, "findadd");

    // exact=true: точное совпадение по тегу. Иначе «Abba» притянула бы и трибьюты,
    // а добавление альбома собрало бы чужие трэки. Фильтр собирает libmpdclient —
    // экранирование кавычек на протоколе двойное, руками его писать нельзя.
    if (!mpd_search_add_db_songs(MPD, true))
        return;

    bool ok = true;
    for (const MpdTagConstraint &c : constraints) {
        const QByteArray v = c.value.toUtf8();
        ok = ok && mpd_search_add_tag_constraint(MPD, MPD_OPERATOR_DEFAULT,
                                                 c.tag, v.constData());
    }
    ok = ok && mpd_search_commit(MPD);

    if (!ok)
        mpd_search_cancel(MPD);
}

void MpdCommands::loadStoredPlaylist(const QString &name) {
    CHECK;
    const QByteArray n = name.toUtf8();
    if (n.isEmpty())
        return;
    RUN("load", mpd_run_load(MPD, n.constData()));
}

void MpdCommands::deletePlaylistPosition(const QString &name, int position) {
    CHECK;
    const QByteArray n = name.toUtf8();
    if (n.isEmpty() || position < 0)
        return;
    RUN("playlistdelete", mpd_run_playlist_delete(MPD, n.constData(),
                                                  static_cast<unsigned>(position)));
}

void MpdCommands::appendToStoredPlaylist(const QString &name, const QString &uri) {
    CHECK;
    const QByteArray n = name.toUtf8();
    const QByteArray u = uri.toUtf8();
    if (n.isEmpty() || u.isEmpty())
        return;

    // Каталог сервер разворачивает сам (`playlistadd "список" "sub"` → строка sub/c1.flac,
    // замер 17), поэтому отдельной ветки для папок не нужно. Дубликаты он пропускает —
    // список из двух одинаковых строк это норма, а не ошибка (замер 16).
    RUN("playlistadd", mpd_run_playlist_add(MPD, n.constData(), u.constData()));
}

void MpdCommands::createEmptyStoredPlaylist(const QString &name) {
    CHECK;
    const QByteArray n = name.toUtf8();
    if (n.isEmpty())
        return;

    // `playlistclear` по имени без файла отвечает ACK 50 No such playlist и списка не
    // создаёт, а команды «создать пустой» у MPD нет вовсе. Поэтому save текущей очереди
    // под этим именем (файл появляется или перезаписывается) плюс clear — очередь
    // пользователя при этом остаётся на месте.
    RUN("save", mpd_run_save(MPD, n.constData()));
    RUN("playlistclear", mpd_run_playlist_clear(MPD, n.constData()));
}

void MpdCommands::movePlaylistBlock(const QString &name, int fromPos, int count, int toPos) {
    CHECK;
    const QByteArray n = name.toUtf8();
    if (n.isEmpty() || fromPos < 0 || count <= 0 || toPos < 0)
        return;

    // TO — координата в списке без вынутого блока: MPD сначала убирает блок, потом
    // вставляет его по указанному индексу (замер 16). Граница `to ≤ len−count` —
    // это «в конец»; большее значение сервер отвергает ACK 2 Bad range.
    if (count == 1) {
        RUN("playlistmove", mpd_run_playlist_move(MPD, n.constData(),
                                                  static_cast<unsigned>(fromPos),
                                                  static_cast<unsigned>(toPos)));
        return;
    }
    RUN("playlistmovelrange",
        mpd_run_playlist_move_range(MPD, n.constData(), static_cast<unsigned>(fromPos),
                                    static_cast<unsigned>(fromPos + count),   // end — исключающий
                                    static_cast<unsigned>(toPos)));
}

void MpdCommands::removePlaylistPositions(const QString &name, int firstRow, int lastRow) {
    CHECK;
    const QByteArray n = name.toUtf8();
    if (n.isEmpty() || firstRow < 0 || lastRow < firstRow)
        return;

    // Диапазонная форма принимает полуинтервал [start; end), поэтому к последней строке
    // прибавляется единица. Сырое «playlistdelete name start end» сервер не понимает
    // («wrong number of arguments», замер 17) — работает именно библиотечная обёртка.
    RUN("playlistdelrange",
        mpd_run_playlist_delete_range(MPD, n.constData(),
                                      static_cast<unsigned>(firstRow),
                                      static_cast<unsigned>(lastRow + 1)));
}

// -------------------------------------------------------------------
//  Playback modes
// -------------------------------------------------------------------

void MpdCommands::setRepeat(bool o) { CHECK; RUN("repeat", mpd_run_repeat(MPD, o)); }
void MpdCommands::setRandom(bool o) { CHECK; RUN("random", mpd_run_random(MPD, o)); }
void MpdCommands::setSingle(bool o) { CHECK; RUN("single", mpd_run_single(MPD, o)); }
void MpdCommands::setConsume(bool o){ CHECK; RUN("consume", mpd_run_consume(MPD, o)); }

void MpdCommands::toggleRepeat()    { CHECK; auto st = conn_->fetchStatus(); RUN("repeat", mpd_run_repeat(MPD, !st.repeat)); }
void MpdCommands::toggleRandom()    { CHECK; auto st = conn_->fetchStatus(); RUN("random", mpd_run_random(MPD, !st.random)); }
void MpdCommands::toggleSingle()    { CHECK; auto st = conn_->fetchStatus(); RUN("single", mpd_run_single(MPD, !st.single)); }
void MpdCommands::toggleConsume()   { CHECK; auto st = conn_->fetchStatus(); RUN("consume", mpd_run_consume(MPD, !st.consume)); }

// -------------------------------------------------------------------
//  Статистика сервера (окно «Server Information»)
// -------------------------------------------------------------------

namespace {

/// Освобождает mpd_stats на любом выходе из блока — в т.ч. по раннему return.
struct StatsGuard {
    struct mpd_stats *s;
    ~StatsGuard() { if (s != nullptr) mpd_stats_free(s); }
};

/** @brief Схемы URL, объявленные сервером (`urlhandlers`).
 *
 *  Общий код для serverStats() и urlSchemes(): окну нужен готовый снимок целиком,
 * а тестам — возможность спросить список отдельно.
 */
QStringList readUrlSchemes(Connection *conn)
{
    QStringList out;
    if (!conn->isConnected())
        return out;

    Connection::ResponseScope scope(*conn, "urlhandlers");
    mpd_send_list_url_schemes(conn->raw());
    while (struct mpd_pair *pair = mpd_recv_url_scheme_pair(conn->raw())) {
        const QString scheme = pair->value ? QString::fromUtf8(pair->value) : QString();
        if (!scheme.isEmpty() && !out.contains(scheme))
            out << scheme;
        mpd_return_pair(conn->raw(), pair);
    }
    return out;   // ResponseScope завершит ответ и снимет error-state
}

} // namespace

QString formatSpan(quint64 seconds)
{
    if (seconds == 0)
        return QStringLiteral("0 s");

    const quint64 d = seconds / 86400;
    const quint64 h = (seconds % 86400) / 3600;
    const quint64 m = (seconds % 3600) / 60;
    const quint64 s = seconds % 60;

    QStringList parts;
    if (d > 0)
        parts << QStringLiteral("%1 d").arg(d);
    if (h > 0)
        parts << QStringLiteral("%1 h").arg(h);
    if (m > 0)
        parts << QStringLiteral("%1 m").arg(m);
    // Секунды показываем, только когда они не ноль: «1 h» точнее и короче, чем
    // «1 h 0 s». Полностью нулевое время уже отсечено выше.
    if (s > 0)
        parts << QStringLiteral("%1 s").arg(s);
    return parts.join(QLatin1Char(' '));
}

std::optional<ServerStats> MpdCommands::serverStats() const
{
    if (!conn_->isConnected())
        return std::nullopt;

    Connection::ResponseScope scope(*conn_, "stats");
    struct mpd_stats *raw = mpd_run_stats(MPD);
    StatsGuard guard{raw};
    if (raw == nullptr)
        return std::nullopt;   // ResponseScope снимет error-state

    ServerStats out;
    // Версия из приветствия сервера («OK MPD 0.25.6») — то же, что показывал оригинал
    // через mpd_server_get_version(). В самом ответе `stats` её нет.
    if (const unsigned *v = mpd_connection_get_server_version(MPD)) {
        out.version = QStringLiteral("%1.%2.%3").arg(v[0]).arg(v[1]).arg(v[2]);
    }
    out.uptimeSec     = mpd_stats_get_uptime(raw);
    out.playtimeSec   = mpd_stats_get_play_time(raw);
    out.dbPlaytimeSec = mpd_stats_get_db_play_time(raw);
    // Аксессоры отдают unsigned long; в int кладём осознанно — миллиарда песен не бывает.
    out.artists       = static_cast<int>(mpd_stats_get_number_of_artists(raw));
    out.albums        = static_cast<int>(mpd_stats_get_number_of_albums(raw));
    out.songs         = static_cast<int>(mpd_stats_get_number_of_songs(raw));

    // Отдельная команда: в ответе `stats` схем нет, а окну они нужны той же строкой.
    out.urlHandlers = readUrlSchemes(conn_);
    return out;
}

QStringList MpdCommands::urlSchemes() const
{
    return readUrlSchemes(conn_);
}

QStringList MpdCommands::tagValues(enum mpd_tag_type tag) const
{
    QStringList out;
    if (!conn_->isConnected())
        return out;

    Connection::ResponseScope scope(*conn_, "list");
    if (!mpd_search_db_tags(MPD, tag))
        return out;   // сервер старше 0.12: команды `list <tag>` нет

    if (!mpd_search_commit(MPD)) {
        mpd_search_cancel(MPD);
        return out;
    }

    while (struct mpd_pair *pair = mpd_recv_pair_tag(MPD, tag)) {
        const QString value = pair->value ? QString::fromUtf8(pair->value) : QString();
        if (!value.isEmpty() && !out.contains(value))
            out << value;
        mpd_return_pair(MPD, pair);
    }
    return out;   // ResponseScope завершит ответ и снимет error-state
}

QVector<enum mpd_tag_type> MpdCommands::supportedTags() const
{
    QVector<enum mpd_tag_type> out;
    if (!conn_->isConnected())
        return out;

    Connection::ResponseScope scope(*conn_, "tagtypes");
    // `list tagtypes` — только включённые теги; имя разбираем тем же парсером, что и
    // сервер в своих ответах (регистр не наш).
    mpd_send_list_tag_types(MPD);
    while (struct mpd_pair *pair = mpd_recv_tag_type_pair(MPD)) {
        const enum mpd_tag_type tag =
            pair->value ? mpd_tag_name_parse(pair->value) : MPD_TAG_UNKNOWN;
        if (tag != MPD_TAG_UNKNOWN && !out.contains(tag))
            out << tag;
        mpd_return_pair(MPD, pair);
    }
    return out;   // ResponseScope завершит ответ и снимет error-state
}

std::optional<TagPlaytime> MpdCommands::tagPlaytime(enum mpd_tag_type tag,
                                                    const QString &value) const
{
    if (!conn_->isConnected() || value.isEmpty())
        return std::nullopt;

    Connection::ResponseScope scope(*conn_, "count");
    // `count` вместо поиска песен: сервер сам складывает длительности, а нам нужна
    // ровно одна пара чисел на значение тега.
    if (!mpd_count_db_songs(MPD))
        return std::nullopt;

    const QByteArray v = value.toUtf8();
    bool ok = mpd_search_add_tag_constraint(MPD, MPD_OPERATOR_DEFAULT, tag, v.constData());
    ok = ok && mpd_search_commit(MPD);
    if (!ok) {
        mpd_search_cancel(MPD);
        return std::nullopt;
    }

    struct mpd_stats *raw = mpd_recv_stats(MPD);
    StatsGuard guard{raw};
    if (raw == nullptr)
        return std::nullopt;

    TagPlaytime out;
    out.value       = value;
    out.playTimeSec = mpd_stats_get_play_time(raw);
    out.songs       = static_cast<int>(mpd_stats_get_number_of_songs(raw));
    return out;   // ResponseScope завершит ответ и снимет error-state
}

void MpdCommands::muteToggle() {
    CHECK;
    static int prevVol = 50;
    auto st = conn_->fetchStatus();
    if (st.volume > 0) {
        prevVol = st.volume;
        RUN("setvol", mpd_run_set_volume(MPD, 0));
    } else {
        RUN("setvol", mpd_run_set_volume(MPD, prevVol));
    }
}
