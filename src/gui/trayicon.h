#ifndef GMPCQT_TRAYICON_H
#define GMPCQT_TRAYICON_H

/**
 * @file trayicon.h
 * @brief System-tray icon with popup menu.
 *
 * Provides quick access to play/pause/stop/next/prev and application
 * quit from the system notification area.  The icon reflects the
 * current play state (playing, paused, stopped).
 */

#include <QSystemTrayIcon>
#include "core/connection.h"

class MpdCommands;

/// Tray icon with a popup menu for MPD transport.
class TrayIcon : public QObject {
    Q_OBJECT
public:
    explicit TrayIcon(Connection *conn, MpdCommands *cmds,
                      QObject *parent = nullptr);

    /// Update tray icon and tooltip from a fresh MPD status snapshot.
    void updateStatus(const Connection::Status &s);

private:
    /// Build the right-click context menu.
    void setupMenu();

    QSystemTrayIcon *icon_;
    Connection *conn_;
    MpdCommands *cmds_;
};

#endif // GMPCQT_TRAYICON_H
