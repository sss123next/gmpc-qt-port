/**
 * @file mpd-commands.cpp
 * @brief Implementation of MPD player-command wrappers.
 *
 * Each function uses the CHECK prelude (returns silently if not connected)
 * and the MPD macro (expands to conn_->raw() → mpd_connection*).
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

// -------------------------------------------------------------------
//  Transport
// -------------------------------------------------------------------

void MpdCommands::play()            { CHECK; mpd_run_play(MPD); }
void MpdCommands::pause()           { CHECK; mpd_run_pause(MPD, true); }
void MpdCommands::togglePause() {
    CHECK;
    auto st = conn_->fetchStatus();
    mpd_run_pause(MPD, st.state != MPD_STATE_PAUSE);
}
void MpdCommands::stop()            { CHECK; mpd_run_stop(MPD); }
void MpdCommands::next()            { CHECK; mpd_run_next(MPD); }
void MpdCommands::previous()        { CHECK; mpd_run_previous(MPD); }

// -------------------------------------------------------------------
//  Seeking
// -------------------------------------------------------------------

void MpdCommands::seek(int s) {
    // Third argument of mpd_run_seek_current() is "relative", not "absolute".
    CHECK; mpd_run_seek_current(MPD, s, false);
}

void MpdCommands::seekRelative(int d) {
    CHECK; mpd_run_seek_current(MPD, d, true);
}

// -------------------------------------------------------------------
//  Volume
// -------------------------------------------------------------------

void MpdCommands::setVolume(int v)  { CHECK; mpd_run_set_volume(MPD, v); }
void MpdCommands::volumeUp(int s)   { CHECK; auto st = conn_->fetchStatus(); mpd_run_set_volume(MPD, st.volume + s); }
void MpdCommands::volumeDown(int s) { CHECK; auto st = conn_->fetchStatus(); mpd_run_set_volume(MPD, st.volume - s); }

// -------------------------------------------------------------------
//  Queue manipulation
// -------------------------------------------------------------------

void MpdCommands::addUri(const QString &uri) {
    CHECK;
    const QByteArray u = uri.toUtf8();
    mpd_run_add(MPD, u.constData());
}

void MpdCommands::playPos(int pos) {
    CHECK;
    mpd_run_play_pos(MPD, static_cast<unsigned>(pos));
}

void MpdCommands::clearQueue() {
    CHECK;
    mpd_run_clear(MPD);
}

// -------------------------------------------------------------------
//  Playback modes
// -------------------------------------------------------------------

void MpdCommands::setRepeat(bool o) { CHECK; mpd_run_repeat(MPD, o); }
void MpdCommands::setRandom(bool o) { CHECK; mpd_run_random(MPD, o); }
void MpdCommands::setSingle(bool o) { CHECK; mpd_run_single(MPD, o); }
void MpdCommands::setConsume(bool o){ CHECK; mpd_run_consume(MPD, o); }

void MpdCommands::toggleRepeat()    { CHECK; auto st = conn_->fetchStatus(); mpd_run_repeat(MPD, !st.repeat); }
void MpdCommands::toggleRandom()    { CHECK; auto st = conn_->fetchStatus(); mpd_run_random(MPD, !st.random); }
void MpdCommands::toggleSingle()    { CHECK; auto st = conn_->fetchStatus(); mpd_run_single(MPD, !st.single); }
void MpdCommands::toggleConsume()   { CHECK; auto st = conn_->fetchStatus(); mpd_run_consume(MPD, !st.consume); }

void MpdCommands::muteToggle() {
    CHECK;
    static int prevVol = 50;
    auto st = conn_->fetchStatus();
    if (st.volume > 0) {
        prevVol = st.volume;
        mpd_run_set_volume(MPD, 0);
    } else {
        mpd_run_set_volume(MPD, prevVol);
    }
}
