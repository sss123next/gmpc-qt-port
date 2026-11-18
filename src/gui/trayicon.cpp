#include "trayicon.h"
#include "mainwindow.h"
#include "mini-player.h"
#include "core/mpd-commands.h"
#include "core/playlist-model.h"
#include "core/config.h"

#include <QMenu>
#include <QApplication>
#include <QStyle>
#include <QRect>

TrayIcon::TrayIcon(Connection *conn, MpdCommands *cmds, QWidget *mainWindow,
                   QObject *parent)
    : QObject(parent), conn_(conn), cmds_(cmds), mainWindow_(mainWindow)
{
    icon_ = new QSystemTrayIcon(this);
    icon_->setIcon(qApp->style()->standardIcon(QStyle::SP_MediaPlay));
    icon_->setToolTip(tr("gmpc-qt — Disconnected"));

    setupMenu();

    // Left-click toggles main window visibility.
    // Double-click shows Mini Player (if attached and enabled).
    connect(icon_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick && miniPlayer_
            && Config::instance().miniPlayerEnabled()) {
            toggleMiniPlayer();
        } else if (reason == QSystemTrayIcon::Trigger) {
            if (mainWindow_->isVisible())
                mainWindow_->hide();
            else {
                mainWindow_->show();
                mainWindow_->raise();
                mainWindow_->activateWindow();
            }
        }
    });

    icon_->show();
}

void TrayIcon::setupMenu()
{
    auto *menu = new QMenu();

    auto *showHide = menu->addAction(tr("Show / Hide"));
    connect(showHide, &QAction::triggered, this, [this] {
        if (mainWindow_->isVisible()) {
            mainWindow_->hide();
        } else {
            mainWindow_->show();
            mainWindow_->raise();
            mainWindow_->activateWindow();
        }
    });
    auto *prefsAct = menu->addAction(tr("Preferences…"));
    connect(prefsAct, &QAction::triggered, this, [this] {
        auto *mw = qobject_cast<MainWindow *>(mainWindow_);
        if (mw)
            mw->showPreferences();
    });
    menu->addSeparator();

    auto *miniPlayerAct = menu->addAction(tr("Mini Player"));
    miniPlayerAct->setCheckable(true);
    miniPlayerAct->setEnabled(Config::instance().miniPlayerEnabled());
    connect(miniPlayerAct, &QAction::triggered, this, [this] {
        toggleMiniPlayer();
    });
    miniPlayerAct_ = miniPlayerAct;

    // Refresh action states on every menu open (settings may have changed).
    connect(menu, &QMenu::aboutToShow, this, [this] {
        if (miniPlayerAct_) {
            miniPlayerAct_->setEnabled(
                Config::instance().miniPlayerEnabled());
            miniPlayerAct_->setChecked(
                miniPlayer_ && miniPlayer_->isVisible());
        }
    });
    menu->addSeparator();

    auto *playPause = menu->addAction(tr("Play/Pause"));
    connect(playPause, &QAction::triggered, cmds_, &MpdCommands::togglePause);

    auto *stop = menu->addAction(tr("Stop"));
    connect(stop, &QAction::triggered, cmds_, &MpdCommands::stop);

    auto *next = menu->addAction(tr("Next"));
    connect(next, &QAction::triggered, cmds_, &MpdCommands::next);

    auto *prev = menu->addAction(tr("Previous"));
    connect(prev, &QAction::triggered, cmds_, &MpdCommands::previous);

    menu->addSeparator();

    auto *quit = menu->addAction(tr("Quit"));
    connect(quit, &QAction::triggered, this, [this] {
        // Quit means "exit", not "hide": bypass close-to-tray.
        auto *mw = qobject_cast<MainWindow *>(mainWindow_);
        if (mw)
            mw->handleIpcQuit();
        else
            QApplication::quit();
    });

    icon_->setContextMenu(menu);
}

void TrayIcon::setMiniPlayer(MiniPlayer *popup)
{
    miniPlayer_ = popup;
}

void TrayIcon::toggleMiniPlayer()
{
    if (!miniPlayer_ || !Config::instance().miniPlayerEnabled())
        return;

    if (miniPlayer_->isVisible())
        miniPlayer_->hide();
    else
        miniPlayer_->show();
}

void TrayIcon::updateStatus(const Connection::Status &s, const SongEntry *song)
{
    // Update tooltip.
    QString tip;
    if (s.state == MPD_STATE_PLAY && song) {
        tip = QStringLiteral("%1 — %2").arg(song->artist, song->title);
        if (!song->album.isEmpty())
            tip += QStringLiteral(" [%1]").arg(song->album);
    } else if (s.state == MPD_STATE_PLAY) {
        tip = tr("gmpc-qt — Playing");
    } else if (s.state == MPD_STATE_PAUSE) {
        tip = tr("gmpc-qt — Paused");
    } else {
        tip = tr("gmpc-qt — Stopped");
    }
    icon_->setToolTip(tip);
}

void TrayIcon::setVisible(bool v)
{
    icon_->setVisible(v);
}

void TrayIcon::showMessage(const QString &title, const QString &msg,
                           QSystemTrayIcon::MessageIcon icon, int msecs)
{
    icon_->showMessage(title, msg, icon, msecs);
}
