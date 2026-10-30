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
#include <QSplitter>
#include <QLabel>
#include <QVBoxLayout>
#include <QShortcut>
#include <QMenu>
#include <QMessageBox>
#include <QCloseEvent>
#include <QStandardPaths>

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

    setupMenuBar();
    setupShortcuts();

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

    // Main splitter: sidebar | content
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    setupSidebar();
    setupCentralArea();

    splitter->addWidget(sidebar_);
    splitter->addWidget(centralStack_);
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
}

void MainWindow::setupMenuBar()
{
    auto *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Preferences..."), this, &MainWindow::showPreferences);
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

    // DatabaseBrowser: MusicDatabaseModel::data doesn't carry URI yet.
    // Only enable for album/track level items (row 2+ at level 1).
    // Currently tracks have no URI stored — skip for now.

    // CurrentPlaylist: already playing — context menu not critical.
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

    // Volume: Ctrl+Plus / Ctrl+Minus
    auto *volUp = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus), this);
    connect(volUp, &QShortcut::activated, this, [this] { cmds_->volumeUp(); });

    auto *volDown = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus), this);
    connect(volDown, &QShortcut::activated, this, [this] { cmds_->volumeDown(); });

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
    connect(fs, &QShortcut::activated, this, [this] {
        fullscreen_ = !fullscreen_;
        if (fullscreen_)
            showFullScreen();
        else
            showNormal();
    });
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
    cfg.sync();
    event->accept();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
}
