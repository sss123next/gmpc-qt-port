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
#include <QTabBar>
#include <QTreeView>
#include <QLabel>
#include <QTimer>
#include <QAbstractNativeEventFilter>
#include <xcb/xcb_keysyms.h>

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
class MprisPlayer;

/// Top-level window composing all UI panels.
class MainWindow : public QMainWindow {
    Q_OBJECT

    /// Intercepts X11 XF86Audio* keypresses from media keyboards.
    class MediaKeyFilter : public QAbstractNativeEventFilter {
    public:
        explicit MediaKeyFilter(MpdCommands *cmds);
        ~MediaKeyFilter() override;
        bool nativeEventFilter(const QByteArray &eventType,
                               void *message, qintptr *result) override;
    private:
        MpdCommands *cmds_;
        xcb_key_symbols_t *keysyms_ = nullptr;
    };
    friend class MediaKeyFilter;

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
    void setupSidebarLabel();
    void setupCentralArea();
    void setupBrowserTabs();
    void connectSignals();
    void tryAutoConnect();

    Connection *conn_;
    MpdCommands *cmds_;
    PlaylistModel *playlistModel_;
    MetadataManager *metadataMgr_;

    // Browser navigation
    QWidget *sidebarPanel_;           ///< Wraps sidebarLabel_ + sidebar_.
    QLabel *sidebarLabel_;            ///< Artist image / label above sidebar tree.
    QTreeView *sidebar_;              ///< Category tree (sidebar).
    QTabBar *tabBar_;                 ///< Breadcrumb tabs above central stack.
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

    // MPRIS
    MprisPlayer *mpris_ = nullptr;

    // Keyboard shortcuts
    void setupShortcuts();
    void setupMediaKeys();

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

