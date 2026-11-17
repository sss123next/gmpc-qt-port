#pragma once

/**
 * @file trayicon.h
 * @brief System-tray icon with popup menu and rich tooltip.
 *
 * Provides quick access to play/pause/stop/next/prev, show/hide window.
 * Tooltip reflects the current song info when playing.
 */

#include <QSystemTrayIcon>
#include <QAction>
#include "core/connection.h"
#include "core/playlist-model.h"

class MpdCommands;
class MiniPlayer;

/// Tray icon with a popup menu for MPD transport and window management.
class TrayIcon : public QObject {
    Q_OBJECT
public:
    /// @param mainWindow The top-level QMainWindow (for show/hide toggles).
    explicit TrayIcon(Connection *conn, MpdCommands *cmds,
                      QWidget *mainWindow, QObject *parent = nullptr);

    /// Update tray icon and tooltip from a fresh MPD status snapshot.
    void updateStatus(const Connection::Status &s, const SongEntry *song = nullptr);

    /// Show or hide the tray icon.
    void setVisible(bool v);
    /// Whether the tray icon is currently visible.
    bool isVisible() const { return icon_->isVisible(); }

    /// Show a balloon notification from the tray icon.
    void showMessage(const QString &title, const QString &msg,
                     QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information,
                     int msecs = 10000);

    /// Attach the mini-player window so the tray can toggle it.
    void setMiniPlayer(MiniPlayer *popup);

private:
    /// Build the right-click context menu.
    void setupMenu();

    /// Toggle the mini-player (show/hide with current song).
    void toggleMiniPlayer();

    QSystemTrayIcon *icon_;
    Connection *conn_;
    MpdCommands *cmds_;
    QWidget *mainWindow_;
    MiniPlayer *miniPlayer_ = nullptr;
    QAction *miniPlayerAct_ = nullptr;
};

