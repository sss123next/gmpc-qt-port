#include "mainwindow.h"
#include "controlbar.h"
#include "statusbar.h"
#include "trayicon.h"
#include "preferences-dialog.h"

#include "core/connection.h"
#include "core/mpd-commands.h"
#include "core/playlist-model.h"
#include "core/config.h"

#include "metadata/metadata-manager.h"
#include "widgets/cover-widget.h"
#include "widgets/song-info.h"
#include "widgets/volume-widget.h"

#include "browsers/current-playlist.h"
#include "browsers/database-browser.h"
#include "browsers/file-browser.h"
#include "browsers/search-browser.h"
#include "browsers/playlist-editor.h"

#include "browsers/music-database-model.h"
#include "browsers/file-system-model.h"

#include <functional>
#include <QStandardItemModel>
#include <QApplication>
#include <QMenuBar>
#include <QMouseEvent>
#include <QSplitter>
#include <QLabel>
#include <QVBoxLayout>
#include <QShortcut>
#include <QMenu>
#include <QMessageBox>
#include <QCloseEvent>
#include <QStandardPaths>
#include <xcb/xcb_keysyms.h>

// ── MediaKeyFilter ───────────────────────────────────────────────────────────

MainWindow::MediaKeyFilter::MediaKeyFilter(MpdCommands *cmds)
    : cmds_(cmds)
{
    // Connect to the X display via the default xcb connection.
    // On Wayland this will be nullptr and nativeEventFilter bails out.
    xcb_connection_t *c = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(c)) {
        xcb_disconnect(c);
        return;
    }
    keysyms_ = xcb_key_symbols_alloc(c);
    xcb_disconnect(c);
}

bool MainWindow::MediaKeyFilter::nativeEventFilter(
    const QByteArray &eventType, void *message, qintptr * /*result*/)
{
    if (eventType != "xcb_generic_event_t")
        return false;
    if (!keysyms_)
        return false;

    auto *ev = static_cast<xcb_generic_event_t *>(message);
    if ((ev->response_type & 0x7f) != XCB_KEY_PRESS)
        return false;

    auto *kp = reinterpret_cast<xcb_key_press_event_t *>(ev);
    xcb_keycode_t *codes = xcb_key_symbols_get_keycode(keysyms_, kp->detail);
    if (!codes)
        return false;

    xcb_keysym_t ks = xcb_key_symbols_get_keysym(keysyms_, kp->detail, 0);
    free(codes);

    switch (ks) {
    case 0x1008ff14: // XF86AudioPrev
    case 0x1008ff16: // XF86AudioRewind
        cmds_->previous();
        return true;
    case 0x1008ff17: // XF86AudioNext
    case 0x1008ff19: // XF86AudioForward
        cmds_->next();
        return true;
    case 0x1008ff13: // XF86AudioStop
        cmds_->stop();
        return true;
    case 0x1008ff15: // XF86AudioPause
        cmds_->pause();
        return true;
    case 0x1008ff11: // XF86AudioPlay
        cmds_->play();
        return true;
    case 0x1008ff12: // XF86AudioPrev (alternative as XF86AudioPlayPause — but let's map to toggle)
        cmds_->togglePause();
        return true;
    default:
        return false;
    }
}

MainWindow::MediaKeyFilter::~MediaKeyFilter()
{
    if (keysyms_)
        xcb_key_symbols_free(keysyms_);
}

// ── MainWindow ───────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , conn_(new Connection(this))
    , cmds_(new MpdCommands(conn_, this))
    , playlistModel_(new PlaylistModel(this))
{
    setupUi();
    connectSignals();
    tryAutoConnect();
}

MainWindow::~MainWindow() = default;

// ── UI construction ───────────────────────────────────────────────────────────

void MainWindow::setupUi()
{
    setWindowTitle(tr("gmpc-qt"));
    auto &cfg = Config::instance();
    resize(cfg.windowWidth(), cfg.windowHeight());

    // Restore position (if saved) and maximized state
    const int wx = cfg.windowX(), wy = cfg.windowY();
    if (wx >= 0 && wy >= 0)
        move(wx, wy);
    if (cfg.windowMaximized())
        showMaximized();

    setupMenuBar();
    setupShortcuts();
    setupMediaKeys();
    setupZoomLevels();

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ── Metadata manager ─────────────────────────────────────────────────
    const QString cacheDir = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    metadataMgr_ = new MetadataManager(cacheDir, QString(), this);

    // ── Widgets for the control bar ──────────────────────────────────────
    coverWidget_ = new CoverWidget(this);
    coverWidget_->setMetadataManager(metadataMgr_);
    songInfo_ = new SongInfo(this);
    volumeWidget_ = new VolumeWidget(this);
    connect(volumeWidget_, &VolumeWidget::volumeChanged,
            cmds_, &MpdCommands::setVolume);

    // ── Control bar at top ───────────────────────────────────────────────
    controlBar_ = new ControlBar(cmds_, coverWidget_, songInfo_,
                                 volumeWidget_, this);
    layout->addWidget(controlBar_);

    // Main splitter: sidebar | content (tab bar + stack)
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    setupSidebar();
    setupCentralArea();
    setupBrowserTabs();

    auto *rightPanel = new QWidget(this);
    auto *rightLay = new QVBoxLayout(rightPanel);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(0);
    rightLay->addWidget(tabBar_);
    rightLay->addWidget(centralStack_, 1);

    setupSidebarLabel();
    sidebarPanel_ = new QWidget(this);
    auto *sideLay = new QVBoxLayout(sidebarPanel_);
    sideLay->setContentsMargins(0, 0, 0, 0);
    sideLay->setSpacing(0);
    sideLay->addWidget(sidebarLabel_);
    sideLay->addWidget(sidebar_, 1);

    splitter->addWidget(sidebarPanel_);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({200, 600});
    layout->addWidget(splitter, 1);

    // Status bar at bottom
    statusBar_ = new StatusBar(this);
    layout->addWidget(statusBar_);

    setCentralWidget(central);

    // Tray icon (mainWindow = this for show/hide toggle)
    trayIcon_ = new TrayIcon(conn_, cmds_, this, this);
    trayIcon_->setVisible(cfg.trayEnabled());

    // Start hidden if configured
    if (cfg.startHidden())
        hide();
    else
        show();
}

void MainWindow::setupMenuBar()
{
    auto *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Preferences..."), this, &MainWindow::showPreferences);
    file->addSeparator();

    // ── Connection profiles submenu ──────────────────────────────────
    auto *profileMenu = file->addMenu(tr("Connection"));
    connect(profileMenu, &QMenu::aboutToShow, this, [this, profileMenu] {
        profileMenu->clear();
        auto &cfg = Config::instance();
        const auto profiles = cfg.profiles();
        const QString active = cfg.activeProfile();

        for (const auto &p : profiles) {
            QString label = p.name;
            if (label == active)
                label.prepend(QStringLiteral("✓ "));
            QAction *act = profileMenu->addAction(label);
            act->setCheckable(true);
            act->setChecked(label == active);
            act->setData(p.name);
        }

        profileMenu->addSeparator();

        QAction *disconnect = profileMenu->addAction(tr("Disconnect"));
        connect(disconnect, &QAction::triggered, this, [this] {
            conn_->disconnect();
        });
    });

    connect(profileMenu, &QMenu::triggered, this, [this](QAction *act) {
        const QString name = act->data().toString();
        if (name.isEmpty())
            return;

        auto &cfg = Config::instance();
        const auto profiles = cfg.profiles();
        for (const auto &p : profiles) {
            if (p.name == name) {
                cfg.setActiveProfile(name);
                conn_->disconnect();
                if (!p.socketPath.isEmpty()) {
                    conn_->connectToServer(p.socketPath, 0, p.password);
                } else {
                    conn_->connectToServer(p.host, p.port, p.password);
                }
                break;
            }
        }
    });

    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, qApp, &QApplication::quit);

    auto *view = menuBar()->addMenu(tr("&View"));
    view->addAction(tr("Toggle &Sidebar"), this, [this] {
        sidebar_->setVisible(!sidebar_->isVisible());
    });

    auto *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About"), this, &MainWindow::showAbout);
}

void MainWindow::setupSidebar()
{
    sidebar_ = new QTreeView(this);
    auto *model = new QStandardItemModel(0, 1, sidebar_);
    model->appendRow(new QStandardItem(tr("Current Playlist")));
    model->appendRow(new QStandardItem(tr("Database")));
    model->appendRow(new QStandardItem(tr("Files")));
    model->appendRow(new QStandardItem(tr("Search")));
    model->appendRow(new QStandardItem(tr("Playlist Editor")));
    sidebar_->setModel(model);
    sidebar_->setHeaderHidden(true);
    // TODO: proper QML-style sidebar with icons
}

void MainWindow::setupSidebarLabel()
{
    sidebarLabel_ = new QLabel(this);
    sidebarLabel_->setFixedHeight(128);
    sidebarLabel_->setScaledContents(true);
    sidebarLabel_->setAlignment(Qt::AlignCenter);
    sidebarLabel_->setText(tr("No cover"));
    sidebarLabel_->setStyleSheet(QStringLiteral(
        "QLabel { background: palette(window); color: palette(disabled,text); }"));
}

void MainWindow::setupCentralArea()
{
    centralStack_ = new QStackedWidget(this);

    currentPlaylist_ = new CurrentPlaylist(conn_, playlistModel_, this);
    databaseBrowser_ = new DatabaseBrowser(conn_, this);
    fileBrowser_     = new FileBrowser(conn_, this);
    searchBrowser_   = new SearchBrowser(conn_, this);
    playlistEditor_  = new PlaylistEditor(conn_, this);

    centralStack_->addWidget(currentPlaylist_);
    centralStack_->addWidget(databaseBrowser_);
    centralStack_->addWidget(fileBrowser_);
    centralStack_->addWidget(searchBrowser_);
    centralStack_->addWidget(playlistEditor_);

    centralStack_->setCurrentWidget(currentPlaylist_);

    // ── Context menus for browsers that expose URI via data roles ──────
    // SearchBrowser: SearchResultItem::uri
    setupBrowserContextMenu(searchBrowser_->treeView(),
        [](const QModelIndex &idx) -> QString {
            auto *m = qobject_cast<SearchResultModel *>(
                const_cast<QAbstractItemModel *>(idx.model()));
            if (!m) return {};
            auto *r = m->resultAt(idx.row());
            return r ? r->uri : QString();
        });

    // FileBrowser: FileSystemModel::UriRole returns full path
    setupBrowserContextMenu(fileBrowser_->treeView(),
        [](const QModelIndex &idx) -> QString {
            return idx.data(FileSystemModel::UriRole).toString();
        });

    // DatabaseBrowser: MusicDatabaseModel now carries URI via MusicDbRoles::UriRole
    setupBrowserContextMenu(databaseBrowser_->treeView(),
        [](const QModelIndex &idx) -> QString {
            return idx.data(MusicDbRoles::UriRole).toString();
        });

    // CurrentPlaylist: already playing — context menu not critical.
}

// ── Browser tabs ────────────────────────────────────────────────────────────

void MainWindow::setupBrowserTabs()
{
    tabBar_ = new QTabBar(this);
    tabBar_->addTab(tr("Current"));
    tabBar_->addTab(tr("Database"));
    tabBar_->addTab(tr("Files"));
    tabBar_->addTab(tr("Search"));
    tabBar_->addTab(tr("Playlist Editor"));
    tabBar_->setExpanding(false);

    connect(tabBar_, &QTabBar::currentChanged, this, [this](int idx) {
        if (idx >= 0)
            centralStack_->setCurrentIndex(idx);
    });

    connect(centralStack_, &QStackedWidget::currentChanged, this, [this](int idx) {
        tabBar_->blockSignals(true);
        tabBar_->setCurrentIndex(idx);
        tabBar_->blockSignals(false);
    });
}

// ── Signals ───────────────────────────────────────────────────────────────────

void MainWindow::connectSignals()
{
    connect(conn_, &Connection::connected, this, &MainWindow::onConnected);
    connect(conn_, &Connection::disconnected, this, &MainWindow::onDisconnected);
    connect(conn_, &Connection::statusChanged, this, &MainWindow::onStatusChanged);

    connect(sidebar_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex &idx) {
        if (!idx.isValid()) return;
        centralStack_->setCurrentIndex(idx.row());
    });
}

void MainWindow::tryAutoConnect()
{
    auto &cfg = Config::instance();
    if (!cfg.autoConnect()) return;

    const QString sock = cfg.socketPath();
    if (!sock.isEmpty()) {
        conn_->connectToServer(sock, 0, cfg.password());
    } else {
        conn_->connectToServer(cfg.host(), cfg.port(), cfg.password());
    }
}

void MainWindow::onConnected()
{
    statusBar_->showConnected();
    playlistModel_->loadFromConnection(conn_->raw());
}

void MainWindow::onDisconnected()
{
    statusBar_->showDisconnected();
    playlistModel_->clear();
}

void MainWindow::onStatusChanged(const Connection::Status &s)
{
    controlBar_->updateStatus(s);
    statusBar_->updateStatus(s);
    const SongEntry *song = playlistModel_->currentSong();
    trayIcon_->updateStatus(s, song);

    // Only update song-dependent widgets when the song actually changes.
    if (s.songId == lastSongId_)
        return;
    lastSongId_ = s.songId;

    songInfo_->showSong(song);
    if (song && !song->artist.isEmpty() && !song->album.isEmpty()) {
        coverWidget_->fetchCover(song->artist, song->album);
    }
    volumeWidget_->setVolume(s.volume);
}

void MainWindow::showPreferences()
{
    PreferencesDialog dlg(this);
    dlg.exec();
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About gmpc-qt"),
        tr("gmpc-qt — Modern MPD client\n"
           "Port of the Gnome Music Player Client to Qt.\n\n"
           "Based on the original gmpc (C) Qball Cow 2003-2011"));
}

// ── Keyboard shortcuts ────────────────────────────────────────────────────────

void MainWindow::setupShortcuts()
{
    // Transport: Ctrl+arrows
    auto *prev = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Left), this);
    connect(prev, &QShortcut::activated, cmds_, &MpdCommands::previous);

    auto *next = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Right), this);
    connect(next, &QShortcut::activated, cmds_, &MpdCommands::next);

    auto *play = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Up), this);
    connect(play, &QShortcut::activated, cmds_, &MpdCommands::togglePause);

    auto *stop = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Down), this);
    connect(stop, &QShortcut::activated, cmds_, &MpdCommands::stop);

    // Volume is controlled via VolumeWidget scroll / slider.
    // Ctrl+Plus/Minus are reserved for zoom levels (setupZoomLevels).

    // Playback modes: Ctrl+R, Ctrl+S, Ctrl+T, Ctrl+Y
    auto *repeat = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_R), this);
    connect(repeat, &QShortcut::activated, cmds_, &MpdCommands::toggleRepeat);

    auto *random = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_S), this);
    connect(random, &QShortcut::activated, cmds_, &MpdCommands::toggleRandom);

    auto *single = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_T), this);
    connect(single, &QShortcut::activated, cmds_, &MpdCommands::toggleSingle);

    auto *consume = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y), this);
    connect(consume, &QShortcut::activated, cmds_, &MpdCommands::toggleConsume);

    // Mute: Ctrl+M
    auto *mute = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_M), this);
    connect(mute, &QShortcut::activated, cmds_, &MpdCommands::muteToggle);

    // Fullscreen: F12
    auto *fs = new QShortcut(QKeySequence(Qt::Key_F12), this);
    connect(fs, &QShortcut::activated, this, &MainWindow::toggleFullscreenOverlay);
}

// ── Multimedia keys ─────────────────────────────────────────────────────────

void MainWindow::setupMediaKeys()
{
    auto *filter = new MediaKeyFilter(cmds_);
    qApp->installNativeEventFilter(filter);

    // The filter is a QObject child of the application (no leak);
    // cleaned up on app exit.
}

// ── Zoom levels ──────────────────────────────────────────────────────────────

void MainWindow::setupZoomLevels()
{
    // Ctrl+Plus: zoom in
    auto *zoomIn = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus), this);
    connect(zoomIn, &QShortcut::activated, this, [this] {
        int next = qMin(zoomLevel_ + 1, 2);
        if (next != zoomLevel_)
            setZoomLevel(next);
    });

    // Ctrl+Minus: zoom out
    auto *zoomOut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus), this);
    connect(zoomOut, &QShortcut::activated, this, [this] {
        int prev = qMax(zoomLevel_ - 1, 0);
        if (prev != zoomLevel_)
            setZoomLevel(prev);
    });
}

void MainWindow::setZoomLevel(int level)
{
    zoomLevel_ = level;

    switch (level) {
    case 0: // Full
        sidebarPanel_->show();
        tabBar_->show();
        statusBar_->show();
        break;
    case 1: // No sidebar, breadcrumb stays
        sidebarPanel_->hide();
        tabBar_->show();
        statusBar_->show();
        break;
    case 2: // Compact — only control bar + content, no chrome
        sidebarPanel_->hide();
        tabBar_->hide();
        statusBar_->hide();
        break;
    }
}

// ── Fullscreen overlay ───────────────────────────────────────────────────────

void MainWindow::toggleFullscreenOverlay()
{
    fullscreen_ = !fullscreen_;

    if (fullscreen_) {
        showFullScreen();

        if (!overlayTimer_) {
            overlayTimer_ = new QTimer(this);
            overlayTimer_->setSingleShot(true);
            connect(overlayTimer_, &QTimer::timeout, this, [this] {
                if (fullscreen_ && controlBar_->isVisible())
                    controlBar_->hide();
            });
        }

        // Reset timer on each toggle
        overlayTimer_->start(5000);
    } else {
        showNormal();
        controlBar_->show();
        if (overlayTimer_)
            overlayTimer_->stop();
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (fullscreen_) {
        // Mouse at top of screen: show control bar, restart timer
        if (event->position().y() < 80) {
            if (!controlBar_->isVisible())
                controlBar_->show();
            overlayTimer_->start(5000);
        }
    }
    QMainWindow::mouseMoveEvent(event);
}

// ── Context menus for browsers ───────────────────────────────────────────────

void MainWindow::setupBrowserContextMenu(
    QTreeView *view, std::function<QString(QModelIndex)> getUri)
{
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view, &QTreeView::customContextMenuRequested,
            this, [this, view, getUri](const QPoint &pos) {
        const QModelIndex idx = view->indexAt(pos);
        if (!idx.isValid())
            return;

        const QString uri = getUri(idx);
        if (uri.isEmpty() && !idx.parent().isValid())
            return;  // top-level grouping node without a URI

        QMenu menu(view);

        QAction *playAct = menu.addAction(tr("Play"));
        QAction *addAct  = menu.addAction(tr("Add"));
        QAction *replaceAct = menu.addAction(tr("Replace"));
        menu.addSeparator();
        QAction *infoAct = menu.addAction(tr("Song Info"));

        QAction *chosen = menu.exec(view->mapToGlobal(pos));
        if (chosen == playAct) {
            cmds_->clearQueue();
            cmds_->addUri(uri);
            cmds_->playPos(0);
        } else if (chosen == addAct) {
            cmds_->addUri(uri);
        } else if (chosen == replaceAct) {
            cmds_->clearQueue();
            cmds_->addUri(uri);
            cmds_->playPos(0);
        } else if (chosen == infoAct) {
            songInfo_->showSong(nullptr);  // placeholder — no popup yet
            statusBar_->showMessage(
                tr("Song Info: not implemented as popup yet"), 3000);
        }
    });
}

// ── Events ────────────────────────────────────────────────────────────────────

void MainWindow::closeEvent(QCloseEvent *event)
{
    auto &cfg = Config::instance();
    cfg.setWindowSize(width(), height());
    cfg.setWindowPos(x(), y());
    cfg.setWindowMaximized(isMaximized());
    cfg.sync();
    event->accept();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
}
