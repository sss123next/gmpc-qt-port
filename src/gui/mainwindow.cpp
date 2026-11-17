#include "mainwindow.h"
#include "browser-stack.h"
#include "controlbar.h"
#include "statusbar.h"
#include "trayicon.h"
#include "tray-popup.h"
#include "preferences-dialog.h"

#include "core/connection.h"
#include "core/idle-watcher.h"
#include "core/mpd-commands.h"
#include "core/playlist-model.h"
#include "core/queue-sync.h"
#include "core/config.h"
#include "core/mpd-env.h"

#include "metadata/metadata-manager.h"
#include "widgets/cover-widget.h"
#include "widgets/song-info.h"
#include "widgets/volume-widget.h"

#include "browsers/current-playlist.h"
#include "browsers/database-browser.h"
#include "browsers/file-browser.h"
#include "browsers/search-browser.h"
#include "browsers/playlist-editor.h"
#include "browsers/now-playing.h"

#include "gui/add-url-dialog.h"
#include "gui/server-info-dialog.h"
#include "gui/song-info-dialog.h"

#include "browsers/music-database-model.h"
#include "browsers/file-system-model.h"

#include "mpris/mpris-player.h"

#include <functional>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QMenuBar>
#include <QMouseEvent>
#include <QLabel>
#include <QVBoxLayout>
#include <QShortcut>
#include <QMenu>
#include <QMessageBox>
#include <mpd/playlist.h>

#include <QCloseEvent>
#include <QStandardPaths>
#include <xcb/xcb_keysyms.h>

namespace {

/** @brief Имена сохранённых списков на сервере.
 *
 *  Спрашиваются в момент открытия меню, а не держатся зеркалом: список мог измениться
 *  другим клиентом (idle-событие о правке файла списка приходит не всегда), а подменю
 *  «Add to Playlist» с устаревшими именами — это команды в никуда.
 */
QStringList storedPlaylistNames(Connection *conn)
{
    QStringList names;
    // handle тоже: у мок-соединения в юните сервера нет, а libmpdclient падает на NULL.
    if (!conn || !conn->isConnected() || !conn->hasHandle())
        return names;

    Connection::ResponseScope scope(*conn, "listplaylists");
    if (mpd_send_list_playlists(conn->raw())) {
        while (struct mpd_playlist *pl = mpd_recv_playlist(conn->raw())) {
            const QString path = QString::fromUtf8(mpd_playlist_get_path(pl));
            if (!path.isEmpty())
                names.append(path);
            mpd_playlist_free(pl);
        }
    }
    return names;
}

}  // namespace

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

    // Route through configurable bindings
    const auto action = Config::instance().actionForKey(static_cast<quint32>(ks));
    switch (action) {
    case MediaKeyAction::Play:
        cmds_->play(); return true;
    case MediaKeyAction::Pause:
        cmds_->pause(); return true;
    case MediaKeyAction::PlayPause:
        cmds_->togglePause(); return true;
    case MediaKeyAction::Stop:
        cmds_->stop(); return true;
    case MediaKeyAction::Next:
        cmds_->next(); return true;
    case MediaKeyAction::Previous:
        cmds_->previous(); return true;
    case MediaKeyAction::Rewind:
        cmds_->previous(); return true;
    case MediaKeyAction::Forward:
        cmds_->next(); return true;
    case MediaKeyAction::VolumeUp:
    case MediaKeyAction::VolumeDown:
    case MediaKeyAction::Mute:
        // Not wired yet — reserved for future VolumeWidget integration
        return false;
    case MediaKeyAction::None:
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
    , idle_(new IdleWatcher(this))
    , playlistModel_(new PlaylistModel(this))
    , queueSync_(new QueueSync(this))
{
    setupUi();
    connectSignals();
    tryAutoConnect();
}

MainWindow::~MainWindow()
{
    // explicit disconnect from known senders whose lambda connections
    // use this as context — QObject::disconnect() only removes
    // sender/receiver entries, not the context records in the sender's
    // list, so they survive into ~QObject where the vtable is already
    // promoted and the assertObjectType<MainWindow> check fails.
    // Use the static QObject::disconnect because Connection and other
    // subclasses may hide the member disconnect().
    QObject::disconnect(conn_, nullptr, this, nullptr);
    QObject::disconnect(idle_, nullptr, this, nullptr);
    QObject::disconnect(queueSync_, nullptr, this, nullptr);
    disconnect();
}

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
    metadataMgr_ = new MetadataManager(cacheDir, QString(),
                                        cfg.discogsToken(),
                                        cfg.musicDirectory(), this);

    // ── Widgets for the control bar ──────────────────────────────────────
    coverWidget_ = new CoverWidget(this);
    coverWidget_->setMetadataManager(metadataMgr_);
    songInfo_ = new SongInfo(this);
    volumeWidget_ = new VolumeWidget(this);
    connect(volumeWidget_, &VolumeWidget::volumeChanged,
            cmds_, &MpdCommands::setVolume);

    // ── Control bar at top ───────────────────────────────────────────────
    controlBar_ = new ControlBar(cmds_, songInfo_,
                                 volumeWidget_, this);
    layout->addWidget(controlBar_);

    // Content area: tab bar + stacked browsers
    setupCentralArea();
    setupBrowserTabs();

    auto *rightPanel = new QWidget(this);
    auto *rightLay = new QVBoxLayout(rightPanel);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(0);
    rightLay->addWidget(tabBar_);
    rightLay->addWidget(centralStack_, 1);

    layout->addWidget(rightPanel, 1);

    // Status bar at bottom
    statusBar_ = new StatusBar(this);
    layout->addWidget(statusBar_);

    // Соединяем сигналы, которым нужен уже созданный statusBar_
    connect(playlistEditor_, &PlaylistEditor::statusMessage,
            statusBar_, &StatusBar::showMessage);

    setCentralWidget(central);

    // Tray icon (mainWindow = this for show/hide toggle)
    trayIcon_ = new TrayIcon(conn_, cmds_, this, this);
    trayIcon_->setVisible(cfg.trayEnabled());

    // ── Tray popup (borderless window with cover + info) ──────────
    if (cfg.trayPopupEnabled()) {
        auto *popup = new TrayPopup(this);
        popup->setMetadataManager(metadataMgr_);
        popup->setPosition(
            static_cast<TrayPopup::Position>(cfg.trayPopupPosition()));
        popup->setStylePreset(
            static_cast<TrayPopup::StylePreset>(cfg.trayPopupStyle()));
        popup->setTimeoutSec(cfg.trayPopupTimeout());
        popup->setShowCover(cfg.trayPopupShowCover());
        popup->setShowButtons(cfg.trayPopupShowButtons());
        popup->setOffsets(cfg.trayPopupXOffset(), cfg.trayPopupYOffset());

        // Connect transport buttons
        connect(popup, &TrayPopup::playPauseClicked,
                cmds_, &MpdCommands::togglePause);
        connect(popup, &TrayPopup::stopClicked,
                cmds_, &MpdCommands::stop);
        connect(popup, &TrayPopup::nextClicked,
                cmds_, &MpdCommands::next);
        connect(popup, &TrayPopup::previousClicked,
                cmds_, &MpdCommands::previous);

        trayIcon_->setPopup(popup);
    }

    // MPRIS DBus interface
    mpris_ = new MprisPlayer(conn_, cmds_, this);

    // Start hidden if configured
    if (cfg.startHidden())
        hide();
    else
        show();
}

void MainWindow::setupMenuBar()
{
    auto *file = menuBar()->addMenu(tr("&File"));

    // ── Очередь: адрес, сохранение и очистка ─────────────────────────
    //
    // В оригинале это были команды (`gmpc url`, `save <имя>`, `clear`), а не пункты
    // меню; без IPC им место здесь — рядом с «Preferences», потому что все три
    // действуют на текущей очереди, как и она.
    QAction *addUrlAct = file->addAction(tr("Add &URL..."), this,
                                         &MainWindow::showAddUrl);
    QAction *saveQueueAct = file->addAction(tr("Save Queue as &Playlist..."),
                                            this, &MainWindow::saveCurrentQueue);
    QAction *clearQueueAct = file->addAction(tr("Clear &Queue"), this,
                                             &MainWindow::clearCurrentQueue);

    // Доступность считается при каждом откритии меню: соединение живёт своей
    // жизнью, а пустую очередь сохранять и чистить нечего.
    connect(file, &QMenu::aboutToShow, this,
            [this, addUrlAct, saveQueueAct, clearQueueAct] {
        const bool connected = conn_->isConnected();
        // Адрес отдавать серверу некому вне соединения; очередь пуста — сохранять
        // и чистить нечего. Число строк берём у модели: она зеркалит сервер.
        addUrlAct->setEnabled(connected);
        const bool hasQueue = connected && playlistModel_->rowCount({}) > 0;
        saveQueueAct->setEnabled(hasQueue);
        clearQueueAct->setEnabled(hasQueue);
    });

    file->addSeparator();
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
                // A profile is an explicit target, so its host/port/password
                // stay put; only the timeout may come from MPD_TIMEOUT.
                connectWith(MpdResolvedSettings::fromConfig(
                    p.host, p.port, p.password, p.socketPath,
                    cfg.connectionTimeout(), false));
                break;
            }
        }
    });

    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, qApp, &QApplication::quit);

    auto *view = menuBar()->addMenu(tr("&View"));
    // Оригинал держал этот пункт в меню View («_View server information»).
    view->addAction(tr("Server &Information..."), this, &MainWindow::showServerInfo);

    auto *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About"), this, &MainWindow::showAbout);
}

void MainWindow::setupCentralArea()
{
    centralStack_ = new QStackedWidget(this);
    browserStack_ = new BrowserStack(centralStack_, this);

    currentPlaylist_ = new CurrentPlaylist(conn_, cmds_, playlistModel_, this);
    databaseBrowser_ = new DatabaseBrowser(conn_, this);
    fileBrowser_     = new FileBrowser(conn_, this);
    // Команды нужны поиску для Enter/Insert: результат кладётся в очередь, а Ctrl+Insert
    // ею же её заменяет.
    searchBrowser_   = new SearchBrowser(conn_, cmds_, this);
    playlistEditor_  = new PlaylistEditor(conn_, cmds_, this);
    // Странице нужен менеджер метаданных: она единственный потребитель лирики.
    nowPlaying_      = new NowPlaying(this);
    nowPlaying_->setMetadataManager(metadataMgr_);

    // Order here defines the stack index and must match setupBrowserTabs(),
    browserStack_->addBrowser(nowPlaying_);
    browserStack_->addBrowser(currentPlaylist_);
    browserStack_->addBrowser(databaseBrowser_);
    browserStack_->addBrowser(fileBrowser_);
    browserStack_->addBrowser(searchBrowser_);
    browserStack_->addBrowser(playlistEditor_);

    // Browsers start disabled; onConnected() hands them the connection.
    browserStack_->setConnected(conn_->isConnected());

    // ── Контекстные меню: намерения отдаёт сам браузер, состав пунктов — menuActionsFor
    setupBrowserContextMenu(databaseBrowser_->treeView(),
                            [this] { return databaseBrowser_->selectedIntents(); });
    setupBrowserContextMenu(fileBrowser_->contentView(),
                            [this] { return fileBrowser_->contentIntents(); });
    setupBrowserContextMenu(fileBrowser_->treeView(),
                            [this] { return fileBrowser_->treeIntents(); });
    setupBrowserContextMenu(searchBrowser_->treeView(),
                            [this] { return searchBrowser_->selectedIntents(); });

    // CurrentPlaylist needs no add-to-queue menu: its rows are already in the
    // queue.  It builds its own context menu (play/remove/move/clear/save).

    // ── Двойной клик (row-activated оригинала) ────────────────────────
    setupBrowserActivation(searchBrowser_->treeView(), [this](const QModelIndex &idx) {
        auto *m = qobject_cast<SearchResultModel *>(
            const_cast<QAbstractItemModel *>(idx.model()));
        if (!m) return;
        const SearchResultItem *r = m->resultAt(idx.row());
        if (r && !r->uri.isEmpty()) cmds_->activateUri(r->uri);
    });

    // Правая панель файлов: каталог — навигация, трэк — играть, список — в очередь.
    setupBrowserActivation(fileBrowser_->contentView(), [this](const QModelIndex &idx) {
        switch (static_cast<FileNode::Type>(idx.data(FileSystemModel::TypeRole).toInt())) {
        case FileNode::Directory:
            fileBrowser_->enterDirectory(idx);
            break;
        case FileNode::Song:
            cmds_->activateUri(idx.data(FileSystemModel::UriRole).toString());
            break;
        case FileNode::Playlist:
            // Файл .m3u в файловой системе — это сохранённый список: load, а не addid.
            cmds_->loadStoredPlaylist(idx.data(FileSystemModel::UriRole).toString());
            break;
        default:
            break;   // Root — корень дерева, активировать нечего
        }
    });

    setupBrowserActivation(databaseBrowser_->treeView(), [this](const QModelIndex &idx) {
        const MusicDbActivation act = databaseBrowser_->activationFor(idx);
        if (!act.uri.isEmpty()) cmds_->activateUri(act.uri);
        else if (!act.constraints.isEmpty()) cmds_->findAddSongs(act.constraints);
    });
}

// ── Browser tabs ────────────────────────────────────────────────────────────

void MainWindow::setupBrowserTabs()
{
    tabBar_ = new QTabBar(this);
    tabBar_->addTab(tr("Now Playing"));
    tabBar_->addTab(tr("Current"));
    tabBar_->addTab(tr("Database"));
    tabBar_->addTab(tr("Files"));
    tabBar_->addTab(tr("Search"));
    tabBar_->addTab(tr("Playlist Editor"));
    tabBar_->setExpanding(false);

    connect(tabBar_, &QTabBar::currentChanged, this, [this](int idx) {
        if (idx >= 0)
            browserStack_->setActiveIndex(idx);
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
    connect(conn_, &Connection::errorOccurred, this, [this](const QString &msg) {
        statusBar_->showMessage(tr("MPD: %1").arg(msg), 8000);
    });
    // Текст уже содержит контекст команды («lsinfo: …»), соединение при этом живое —
    // показываем как временное сообщение, не меняя индикатор подключения.
    connect(conn_, &Connection::commandFailed, this, [this](const QString &msg) {
        statusBar_->showMessage(tr("MPD: %1").arg(msg), 8000);
        // Отказ команды означает, что локальная правка очереди (удаление, перенос) на
        // сервере не состоялась. Дифф такого не увидит — версии-то очереди совпадают,
        // поэтому нужен полный перечёт. Он редок и делается вне GUI-потока.
        queueSync_->reload();
    });

    // События MPD (6.7): слушатель держит своё соединение, поэтому статус можно
    // забирать мгновенно, не дожидаясь тика таймера. Polling никуда не делся —
    // он подстраховка, и Connection сам решает, как часто опрашивать, когда
    // событий нет (setIdleWatched). Деревья браузеров от перестройки базы сами не
    // меняются — они читают её по раскрытию узла; а вот размеры и время по тегам в
    // окне статистики устаревают молча, поэтому database|update адресован ему.
    connect(conn_, &Connection::connected, this, [this] {
        const MpdEndpoint endpoint{conn_->host(), conn_->port(),
                                   conn_->password(), conn_->timeoutMs()};
        idle_->setEndpoint(endpoint);
        idle_->start();
        // Очередь перечитывается в своём потоке: 65 тыс. позиций читаются ~914 мс, и в
        // GUI-потоке это зависшее окно (замер — PLAN.md). Заодно здесь, а не в
        // onConnected(): слот подключения зарегистрирован раньше этого обработчика, а
        // без адреса задача на перечтение просто не поставилась бы.
        queueSync_->setEndpoint(endpoint);
        queueSync_->reload();
    });
    connect(conn_, &Connection::disconnected, this, [this] {
        idle_->stop();
        queueSync_->reset();   // версия очереди прежнего соединения больше не действительна
    });

    connect(idle_, &IdleWatcher::mixerChanged, conn_, &Connection::refreshNow);
    connect(idle_, &IdleWatcher::playerChanged, this, [this] {
        conn_->refreshNow();
        // Теги играющего трэка `update` меняет через подсистему player, а не queue: без
        // этого диффа правка дошла бы только к следующему событию очереди. Лишний вызов
        // дешёв — worker сам увидит, что версия очереди не менялась.
        queueSync_->syncNow();
    });
    connect(idle_, &IdleWatcher::queueChanged, this, [this] {
        conn_->refreshNow();   // длина очереди и songid живут в status
        queueSync_->syncNow(); // а сама очередь — диффом plchanges, не listqueue
    });
    // База перестраивается или уже перестроена (idle database|update): в окне
    // статистики устарели и размеры базы, и таблица времени по тегам. Пересборку
    // просим только у открытого окна — закрытое соберётся при следующем показе.
    connect(idle_, &IdleWatcher::databaseChanged, this, [this] {
        if (serverInfo_ == nullptr)
            return;
        serverInfo_->refresh();
        serverInfo_->reloadTagStats();
    });

    connect(idle_, &IdleWatcher::watchingChanged, conn_, &Connection::setIdleWatched);

    // Очередь (7.2): worker присылает либо новый список, либо теги изменившихся строк.
    // Сбрасывать модель на обновлении тегов значит потерять выделение и прокрутку
    // очереди без всякой причины — состав-то прежний.
    connect(queueSync_, &QueueSync::queueReplaced, playlistModel_,
            [this](const QList<SongEntry> &songs, quint32) {
        playlistModel_->setSongs(songs);
    });
    connect(queueSync_, &QueueSync::tagsRefreshed, playlistModel_,
            [this](const QList<SongEntry> &rows) {
        playlistModel_->applyTagsRefreshed(rows);
    });
    connect(queueSync_, &QueueSync::syncFailed, this, [this](const QString &msg) {
        // При разрыве соединения очередь не перечитать — о подключении уже сказал
        // индикатор, вторым сообщением он только мешал бы.
        if (conn_->isConnected())
            statusBar_->showMessage(tr("Queue: %1").arg(msg), 8000);
    });
}

void MainWindow::tryAutoConnect()
{
    auto &cfg = Config::instance();
    if (!cfg.autoConnect()) return;

    // Precedence saved settings > MPD_* > library defaults; Config keeps
    // raw values so an unset field is distinguishable from a saved one.
    connectWith(MpdResolvedSettings::fromConfig(
        cfg.host(), cfg.port(), cfg.password(), cfg.socketPath(),
        cfg.connectionTimeout()));
}

void MainWindow::connectWith(const MpdResolvedSettings &r)
{
    conn_->connectToServer(r.host(), r.port(), r.password(), r.timeoutMs());
}

void MainWindow::onConnected()
{
    statusBar_->showConnected();
    // Очередь сюда приходит от QueueSync (см. connectSignals): перечитывать её здесь
    // означало бы стоять в GUI-потоке все ~914 мс на 65 тыс. позиций.
    browserStack_->setConnected(true);

    // ── Получаем music_directory от MPD для локального поиска обложек ──
    // Если пользователь не указал каталог в Preferences, используем то,
    // что возвращает сервер. Это позволяет локальным обложкам работать
    // без ручной настройки.
    auto &cfg = Config::instance();
    if (cfg.musicDirectory().isEmpty()) {
        const auto mpdDir = cmds_->musicDirectory();
        if (mpdDir.has_value() && !mpdDir->isEmpty()) {
            cfg.setMusicDirectory(*mpdDir);
            metadataMgr_->setMusicDir(*mpdDir);
            qDebug() << "  music_directory from MPD:" << *mpdDir;
        }
    }
}

void MainWindow::onDisconnected()
{
    statusBar_->showDisconnected();
    browserStack_->setConnected(false);
    lastSongId_ = -2;   // гарантирует обновление UI при первом onStatusChanged после реконнекта
    lastVolume_ = -1;
    songInfo_->clear();
    nowPlaying_->setSong(nullptr);
    playlistModel_->clear();
}

void MainWindow::onStatusChanged(const Connection::Status &s)
{
    controlBar_->updateStatus(s);
    statusBar_->updateStatus(s);

    // Volume used to be applied only when the song changed, so a change from
    // another client or MPRIS never reached the widget.  Applying it on every
    // status tick is safe because we skip values already shown.
    if (s.volume >= 0 && s.volume != lastVolume_) {
        lastVolume_ = s.volume;
        volumeWidget_->setVolume(s.volume);
    }

    // MPD alone decides which track plays: `songid` survives queue edits while
    // positions shift, so pass both and let the model resolve them.
    playlistModel_->setCurrentSong(s.songId, s.songPos);
    const SongEntry *song = playlistModel_->currentSong();
    trayIcon_->updateStatus(s, song);

    // Only update song-dependent widgets when the song actually changes.
    const int songId = song ? song->id : -1;
    if (songId == lastSongId_)
        return;
    lastSongId_ = songId;

    songInfo_->showSong(song);
    // Страница «Now Playing» живёт тем же источником истины: трэк ей отдаём здесь,
    // а лирику она тянет сама (ключ внутри гасит повторные запросы).
    nowPlaying_->setSong(song);
    if (song && !song->artist.isEmpty() && !song->album.isEmpty()) {
        coverWidget_->fetchCover(song->artist, song->album, song->uri);
    }

    // ── Tray notification on track change ────────────────────────────
    if (trayIcon_ && trayIcon_->isVisible() && song) {
        QString msg;
        if (!song->artist.isEmpty() && !song->title.isEmpty())
            msg = song->artist + QStringLiteral(" — ") + song->title;
        else if (!song->title.isEmpty())
            msg = song->title;
        else
            msg = song->uri;
        if (!song->album.isEmpty())
            msg += QStringLiteral("  [%1]").arg(song->album);
        trayIcon_->showMessage(tr("Now Playing"), msg,
                               QSystemTrayIcon::Information, 5000);
    }
}

void MainWindow::showPreferences()
{
    PreferencesDialog dlg(this);

    // Fading, выходы и ReplayGain живут на сервере: окно читает их при открытии, а по
    // idle OUTPUT/OPTIONS перечитывает — тумблер мог дёрнуть другой клиент.
    if (cmds_ != nullptr) {
        dlg.bindServer(cmds_);
        connect(idle_, &IdleWatcher::outputsChanged, &dlg,
                [&dlg] { dlg.refreshServerSettings(); });
        connect(idle_, &IdleWatcher::playerChanged, &dlg,
                [&dlg] { dlg.refreshServerSettings(); });
    }

    dlg.exec();
}

void MainWindow::showServerInfo()
{
    // Окно немодальное и самообновляющееся: повторный пункт меню обязан поднять уже
    // открытое, а не плодить копии (указатель обнулится сам — QPointer).
    if (serverInfo_ == nullptr)
        serverInfo_ = new ServerInfoDialog(cmds_, this);

    serverInfo_->show();
    serverInfo_->raise();
    serverInfo_->activateWindow();
}

void MainWindow::showAddUrl()
{
    AddUrlDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    // Пустое поле окно не закроет (кнопка гаснет), но проверка дешёвая: адрес
    // без единого символа серверу отдавать незачем.
    const QString url = dlg.url();
    if (url.isEmpty())
        return;

    cmds_->addUrl(url);
}

void MainWindow::saveCurrentQueue()
{
    currentPlaylist_->saveQueueAs();
}

void MainWindow::clearCurrentQueue()
{
    currentPlaylist_->clearQueueWithConfirm();
    // clearQueue командует серверу, но QueueSync не получает idle за время
    // подтверждения — форсируем перечтение, чтобы модель не осталась с прежними строками.
    queueSync_->reload();
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About gmpc-qt"),
        tr("gmpc-qt — Modern MPD client\n"
           "Port of the Gnome Music Player Client to Qt.\n\n"
           "Based on the original gmpc (C) Qball Cow 2003-2011"));
}

// ── IPC slots ────────────────────────────────────────────────────────────────

void MainWindow::showWindow()
{
    showNormal();       // Де-иконизация, если было минимизировано.
    raise();
    activateWindow();
}

void MainWindow::hideWindow()
{
    // Если трей включён — в трей (как оригинал); иначе просто скрываем окно.
    if (trayIcon_ && trayIcon_->isVisible())
        hide();
    else
        showMinimized();
}

void MainWindow::toggleVisibility()
{
    if (isVisible() && !isMinimized())
        hideWindow();
    else
        showWindow();
}

void MainWindow::handleIpcQuit()
{
    QApplication::quit();
}

void MainWindow::handleIpcShowNotification()
{
    // Последний известный трэк уже есть в трее; если его нет — нет и нотификации.
    if (trayIcon_)
        trayIcon_->showMessage(tr("gmpc-qt"), tr("Playing..."));
}

void MainWindow::handleIpcUrl(const QString &url)
{
    if (url.isEmpty())
        return;

    // Если не подключены — команда уйдёт в никуда (оригинал молча делал то же
    // самое, только с UniqueApp). Соединение могло быть разорвано между моментом
    // отправки IPC-команды и её приёмом.
    if (cmds_ && conn_->isConnected())
        cmds_->addUrl(url);
    else
        qWarning("IPC: cannot handle URL <%s> — not connected",
                 qPrintable(url));
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
        tabBar_->show();
        statusBar_->show();
        break;
    case 1: // No tabs, status bar stays
        tabBar_->hide();
        statusBar_->show();
        break;
    case 2: // Compact — only control bar + content, no chrome
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
    QAbstractItemView *view, std::function<QVector<BrowserIntent>()> gatherIntents)
{
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view, &QAbstractItemView::customContextMenuRequested, this,
            [this, view, gatherIntents](const QPoint &pos) {
        const QModelIndex idx = view->indexAt(pos);
        if (!idx.isValid())
            return;

        // Правый клик по строке вне выделения относится именно к ней: иначе меню
        // предлагало бы команды для строк, выделенных до этого и давно не нужных.
        auto *sel = view->selectionModel();
        if (sel && !sel->isSelected(idx)) {
            sel->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect
                                          | QItemSelectionModel::Rows);
        }

        showBrowserMenu(view, pos, gatherIntents());
    });
}

void MainWindow::showBrowserMenu(QAbstractItemView *view, const QPoint &pos,
                                const QVector<BrowserIntent> &intents)
{
    const QString musicDir = Config::instance().musicDirectory();
    const BrowserMenuActions actions = menuActionsFor(intents, musicDir);
    if (!actions.canAdd && !actions.canCopyPath)
        return;   // меню из одних выключенных пунктов только моргает под курсором

    QMenu menu(view);
    QAction *playAct = menu.addAction(tr("Play"));
    playAct->setEnabled(actions.canPlay);
    QAction *addAct = menu.addAction(tr("Add"));
    addAct->setEnabled(actions.canAdd);
    QAction *replaceAct = menu.addAction(tr("Replace"));
    replaceAct->setEnabled(actions.canReplace);
    // «Info» — только у трэка: показать теги и лирику можно лишь одной песне (7.4 обещал
    // этот пункт, но окна метаданных не было).
    QAction *infoAct = nullptr;
    QString infoUri;
    for (const BrowserIntent &intent : intents) {
        if (intent.kind == BrowserIntent::Kind::Song && !intent.uri.isEmpty()) {
            infoAct = menu.addAction(tr("Info"));
            infoUri = intent.uri;   // первая выделенная песня: окно одно
            break;
        }
    }

    const BrowserPathMenuItems paths = addPathMenuItems(menu, intents, musicDir);

    // «Add to Playlist ▸ имя» — то, чего в оригинале как пункта меню браузера не было:
    // сохранённый список это единственное место помимо очереди, куда строку можно положить.
    // Годятся только пути файлов: у группы и списка URI — не путь (замер 18).
    QVector<QString> appendable;
    for (const BrowserIntent &intent : intents) {
        if (!intent.uri.isEmpty()
            && (intent.kind == BrowserIntent::Kind::Song
                || intent.kind == BrowserIntent::Kind::Directory)) {
            appendable.append(intent.uri);
        }
    }

    QMenu *toList = nullptr;
    if (!appendable.isEmpty()) {
        toList = menu.addMenu(tr("Add to Playlist"));
        const QStringList names = storedPlaylistNames(conn_);
        for (const QString &name : names)
            toList->addAction(name);
        if (names.isEmpty())
            // Пустое подменю выглядит сломанным: сервер без списков — это факт, а не баг.
            toList->addAction(tr("(no playlists)"))->setEnabled(false);
    }

    QAction *chosen = menu.exec(view->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    // Пункт подменю узнаётся по принадлежности: у него нет ни одного из указателей выше,
    // а выключенный «(no playlists)» в exec() выбрать нельзя.
    if (toList && toList->actions().contains(chosen)) {
        // Одна команда на строку: playlistadd принимает ровно один путь, а список-цель
        // уже выбран пунктом подменю.
        for (const QString &uri : appendable)
            cmds_->appendToStoredPlaylist(chosen->text(), uri);
        statusBar_->showMessage(tr("Added to playlist \"%1\"").arg(chosen->text()), 2000);
    } else if (chosen == playAct) {
        // canPlay означает ровно одну строку-трэк, так что первый Song и есть клика.
        for (const BrowserIntent &intent : intents) {
            if (intent.kind == BrowserIntent::Kind::Song) {
                cmds_->activateUri(intent.uri);
                break;
            }
        }
    } else if (chosen == addAct) {
        addIntentsToQueue(intents);
    } else if (chosen == replaceAct) {
        // Как Replace в оригинале: playlist_clear, затем добавление, затем игра.
        cmds_->clearQueue();
        addIntentsToQueue(intents);
        cmds_->play();
    } else if (infoAct && chosen == infoAct) {
        showSongInfo(infoUri);
    } else if (paths.openFolder && chosen == paths.openFolder) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(paths.folder));
    } else if (paths.copyPath && chosen == paths.copyPath) {
        QApplication::clipboard()->setText(copyTextFor(intents, musicDir));
        statusBar_->showMessage(tr("Path copied to clipboard"), 2000);
    }
}

void MainWindow::showSongInfo(const QString &uri)
{
    // Теги берём у сервера по URI: намерение строки несёт только путь, а показать надо
    // artist/title/album. Файла в базе может и не быть — тогда останется хотя бы путь.
    SongEntry song;
    if (const auto found = cmds_->songForUri(uri))
        song = *found;
    else
        song.uri = uri;

    auto *dlg = new SongInfoDialog(song, metadataMgr_, this);
    // Окон можно открыть несколько подряд — пусть закрываются сами.
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::addIntentsToQueue(const QVector<BrowserIntent> &intents)
{
    for (const BrowserIntent &intent : intents) {
        switch (intent.kind) {
        case BrowserIntent::Kind::Song:
        case BrowserIntent::Kind::Directory:
            // `add "sub"` кладёт в очередь весь каталог, а `addid "sub"` отвечает
            // «ACK [50@0] {addid} No such song» (замер на MPD 0.25), поэтому иерархия
            // идёт одним и тем же addUri, а не двумя командами.
            cmds_->addUri(intent.uri);
            break;
        case BrowserIntent::Kind::StoredPlaylist:
            cmds_->loadStoredPlaylist(intent.uri);
            break;
        case BrowserIntent::Kind::TagGroup:
            cmds_->findAddSongs(intent.constraints);
            break;
        }
    }
}

// ── Double-click activation for browsers ────────────────────────────────────

void MainWindow::setupBrowserActivation(
    QAbstractItemView *view, std::function<void(QModelIndex)> activate)
{
    // doubleClicked приходит с индексом вида (для прокси — проксированным); маппинг и
    // разбор типа — забота колбэка, здесь лишь гашение невалидного индекса.
    connect(view, &QAbstractItemView::doubleClicked, this,
            [activate](const QModelIndex &idx) {
                if (idx.isValid())
                    activate(idx);
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

    if (cfg.trayEnabled() && cfg.closeToTray()) {
        hide();
        event->ignore();
    } else {
        event->accept();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
}
