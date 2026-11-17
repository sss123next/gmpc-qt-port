#include "trayicon.h"
#include "core/mpd-commands.h"
#include "core/playlist-model.h"
#include "tray-popup.h"

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

    // Left-click toggles main window visibility; popup (if attached) is
    // shown via a separate action, not on click.
    connect(icon_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
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

void TrayIcon::togglePopup()
{
    if (!popup_)
        return;

    if (popup_->isVisible()) {
        popup_->hide();
        return;
    }

    // Show with last known song data (or just the icon position for clear).
    if (haveSong_)
        popup_->showSong(&lastSong_, iconScreenPos());
    else
        popup_->showSong(nullptr, iconScreenPos());
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
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);

    icon_->setContextMenu(menu);
}

void TrayIcon::updateStatus(const Connection::Status &s, const SongEntry *song)
{
    // Cache for popup use.
    lastStatus_ = s;
    if (song) {
        lastSong_ = *song;
        haveSong_ = true;
    } else {
        haveSong_ = false;
    }

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

    // If popup is currently visible, keep it up-to-date.
    if (popup_ && popup_->isVisible())
        popup_->showSong(haveSong_ ? &lastSong_ : nullptr, iconScreenPos());
}

void TrayIcon::setPopup(TrayPopup *popup)
{
    popup_ = popup;
}

QPoint TrayIcon::iconScreenPos() const
{
    const QRect geo = icon_->geometry();
    if (geo.isValid())
        return geo.center();
    return QCursor::pos();
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
