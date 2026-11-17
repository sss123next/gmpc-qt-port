#pragma once

/**
 * @file trayicon.h
 * @brief System-tray icon with popup menu and rich tooltip.
 *
 * Provides quick access to play/pause/stop/next/prev, show/hide window,
 * and toggle popup with cover art and song info.  Tooltip reflects the
 * current song info when playing.
 */

#include <QSystemTrayIcon>
#include "core/connection.h"
#include "core/playlist-model.h"

class MpdCommands;
class TrayPopup;

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

    /// Attach a popup widget that toggles on tray-icon activation.
    /// Owned by caller (typically MainWindow).
    void setPopup(TrayPopup *popup);

    /// Screen position of the tray icon (for popup positioning).
    QPoint iconScreenPos() const;

private:
    /// Show/hide the attached popup.
    void togglePopup();

    /// Build the right-click context menu.
    void setupMenu();

    QSystemTrayIcon *icon_;
    Connection *conn_;
    MpdCommands *cmds_;
    QWidget *mainWindow_;
    TrayPopup *popup_ = nullptr;

    // Cached song data for popup
    Connection::Status lastStatus_;
    SongEntry lastSong_;
    bool haveSong_ = false;
};

