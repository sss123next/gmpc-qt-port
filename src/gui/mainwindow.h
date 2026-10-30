#pragma once

/**
 * @file mainwindow.h
 * @brief Main application window.
 *
 * Contains the full UI layout: menu bar, control bar, sidebar with
 * browser stack, status bar, and system-tray icon.  Owns the central
 * Connection, MpdCommands, PlaylistModel, and MetadataManager
 * instances.
 */

#include <QMainWindow>
#include <QStackedWidget>
#include <QTreeView>
#include <QTimer>

#include "core/connection.h"

class MpdCommands;
class PlaylistModel;
class ControlBar;
class StatusBar;
class TrayIcon;
class DatabaseBrowser;
class CurrentPlaylist;
class FileBrowser;
class SearchBrowser;
class PlaylistEditor;
class MetadataManager;
class CoverWidget;
class SongInfo;
class VolumeWidget;

/// Top-level window composing all UI panels.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /// The shared MPD connection.
    Connection *connection() const { return conn_; }
    /// Transport-level command helper.
    MpdCommands *commands() const { return cmds_; }

private slots:
    /// Called when the MPD connection is established.
    void onConnected();
    /// Called when the MPD connection is lost.
    void onDisconnected();
    /// Called on every MPD status update (play state, volume, etc.).
    void onStatusChanged(const Connection::Status &s);

    /// Show the preferences dialog.
    void showPreferences();
    /// Show the About dialog.
    void showAbout();

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUi();
    void setupMenuBar();
    void setupSidebar();
    void setupCentralArea();
    void connectSignals();
    void tryAutoConnect();

    Connection *conn_;
    MpdCommands *cmds_;
    PlaylistModel *playlistModel_;
    MetadataManager *metadataMgr_;

    // Browser navigation
    QTreeView *sidebar_;              ///< Category tree (sidebar).
    QStackedWidget *centralStack_;    ///< Right panel stacked browsers.

    // Browsers
    CurrentPlaylist *currentPlaylist_;
    DatabaseBrowser *databaseBrowser_;
    FileBrowser *fileBrowser_;
    SearchBrowser *searchBrowser_;
    PlaylistEditor *playlistEditor_;

    // Bars
    ControlBar *controlBar_;  ///< Transport buttons + cover + info + volume.
    StatusBar *statusBar_;    ///< Connection indicator + messages.

    // Widgets embedded in ControlBar (owned by MainWindow)
    CoverWidget *coverWidget_;
    SongInfo *songInfo_;
    VolumeWidget *volumeWidget_;

    // Tray
    TrayIcon *trayIcon_;      ///< System-tray icon with popup menu.

    // Keyboard shortcuts
    void setupShortcuts();

    // ── Zoom / Fullscreen ───────────────────────────────────────────────▲
    void setupZoomLevels();         ///< Ctrl+Plus/Minus zoom control.
    void setZoomLevel(int level);   ///< 0=full, 1=no-sidebar, 2=compact.
    void toggleFullscreenOverlay(); ///< F12 handler with auto-hide.

    // Context menu helper for tree-view browsers
    void setupBrowserContextMenu(QTreeView *view,
                                 std::function<QString(QModelIndex)> getUri);

    bool fullscreen_ = false; ///< Whether the window is fullscreen.
    int zoomLevel_ = 0;       ///< Current zoom level (0/1/2).
    QTimer *overlayTimer_ = nullptr; ///< Auto-hide timer for fullscreen.

protected:
    void mouseMoveEvent(QMouseEvent *event) override;

    // Cached song-id to avoid redundant cover/songinfo updates.
    int lastSongId_ = -1;
};

