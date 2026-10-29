#include "trayicon.h"
#include "core/mpd-commands.h"

#include <QMenu>
#include <QApplication>
#include <QStyle>

TrayIcon::TrayIcon(Connection *conn, MpdCommands *cmds, QObject *parent)
    : QObject(parent), conn_(conn), cmds_(cmds)
{
    icon_ = new QSystemTrayIcon(this);
    icon_->setIcon(qApp->style()->standardIcon(QStyle::SP_MediaPlay));
    icon_->setToolTip(tr("gmpc-qt — Disconnected"));

    setupMenu();
    icon_->show();
}

void TrayIcon::setupMenu()
{
    auto *menu = new QMenu();

    auto *playPause = menu->addAction(tr("Play/Pause"));
    connect(playPause, &QAction::triggered, cmds_, &MpdCommands::togglePause);

    auto *next = menu->addAction(tr("Next"));
    connect(next, &QAction::triggered, cmds_, &MpdCommands::next);

    auto *prev = menu->addAction(tr("Previous"));
    connect(prev, &QAction::triggered, cmds_, &MpdCommands::previous);

    menu->addSeparator();

    auto *quit = menu->addAction(tr("Quit"));
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);

    icon_->setContextMenu(menu);
}

void TrayIcon::updateStatus(const Connection::Status &s)
{
    QString tip;
    if (s.state == MPD_STATE_PLAY)
        tip = tr("gmpc-qt — Playing");
    else if (s.state == MPD_STATE_PAUSE)
        tip = tr("gmpc-qt — Paused");
    else
        tip = tr("gmpc-qt — Stopped");
    icon_->setToolTip(tip);
}
