#include "mainwindow.h"
#include "controlbar.h"
#include "statusbar.h"
#include "trayicon.h"

#include "core/connection.h"
#include "core/mpd-commands.h"
#include "core/playlist-model.h"
#include "core/config.h"

#include "browsers/current-playlist.h"
#include "browsers/database-browser.h"
#include "browsers/file-browser.h"
#include "browsers/search-browser.h"
#include "browsers/playlist-editor.h"

#include <QStandardItemModel>
#include <QApplication>
#include <QMenuBar>
#include <QSplitter>
#include <QLabel>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QCloseEvent>

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

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Control bar at top
    controlBar_ = new ControlBar(cmds_, this);
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

    // Tray icon
    trayIcon_ = new TrayIcon(conn_, cmds_, this);
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
    if (cfg.autoConnect()) {
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
    trayIcon_->updateStatus(s);
}

void MainWindow::showPreferences()
{
    // TODO: preferences dialog
    QMessageBox::information(this, tr("Preferences"),
                             tr("Preferences dialog not yet implemented."));
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About gmpc-qt"),
        tr("gmpc-qt — Modern MPD client\n"
           "Port of the Gnome Music Player Client to Qt.\n\n"
           "Based on the original gmpc (C) Qball Cow 2003-2011"));
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
