#ifndef GMPCQT_MPD_COMMANDS_H
#define GMPCQT_MPD_COMMANDS_H

/**
 * @file mpd-commands.h
 * @brief Convenience wrapper around MPD player commands.
 *
 * Each public slot maps directly to one or two libmpdclient
 * mpd_run_*() calls.  The raw libmpdclient handle is accessed
 * through Connection::raw() via the MPD macro.
 */

#include <QObject>
#include "connection.h"

/// Stateless set of player-command slots that operate on a Connection.
class MpdCommands : public QObject {
    Q_OBJECT
public:
    /** @brief Construct with an existing (possibly disconnected) connection.
     *  @param conn    Owning Connection — must outlive this object.
     *  @param parent  Qt parent.
     */
    explicit MpdCommands(Connection *conn, QObject *parent = nullptr);

public slots:
    // -- transport ----------------------------------------------------------
    void play();
    void pause();
    void togglePause();
    void stop();
    void next();
    void previous();

    // -- seeking ------------------------------------------------------------
    /** @brief Seek to an absolute position.
     *  @param seconds  Target position.
     */
    void seek(int seconds);

    /** @brief Seek relative to current position.
     *  @param delta  Signed offset in seconds.
     */
    void seekRelative(int delta);

    // -- volume -------------------------------------------------------------
    /** @brief Set volume (0–100). */
    void setVolume(int vol);
    void volumeUp(int step = 5);
    void volumeDown(int step = 5);

    // -- playback modes -----------------------------------------------------
    void setRepeat(bool on);
    void setRandom(bool on);
    void setSingle(bool on);
    void setConsume(bool on);

    void toggleRepeat();
    void toggleRandom();
    void toggleSingle();
    void toggleConsume();

    /// Toggle mute: remembers the last non-zero volume.
    void muteToggle();

private:
    Connection *conn_;
};

#endif // GMPCQT_MPD_COMMANDS_H
