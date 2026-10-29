#ifndef GMPCQT_MAINWINDOW_H
#define GMPCQT_MAINWINDOW_H

/**
 * @file mainwindow.h
 * @brief Main application window.
 *
 * Contains the full UI layout: menu bar, control bar, sidebar with
 * browser stack, status bar, and system-tray icon.  Owns the central
 * Connection, MpdCommands, and PlaylistModel instances.
 */

#include <QMainWindow>
#include <QStackedWidget>
#include <QTreeView>

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
    ControlBar *controlBar_;  ///< Transport buttons + volume + seek.
    StatusBar *statusBar_;    ///< Connection indicator + messages.

    // Tray
    TrayIcon *trayIcon_;      ///< System-tray icon with popup menu.
};

#endif // GMPCQT_MAINWINDOW_H
