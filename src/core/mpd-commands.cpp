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
