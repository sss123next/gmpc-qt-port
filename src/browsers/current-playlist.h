#pragma once

/**
 * @file current-playlist.h
 * @brief Current (now-playing) playlist browser.
 *
 * Displays the MPD queue in a QTreeView bound to a PlaylistModel.
 * This is the default browser shown on startup.
 */

#include "browser-widget.h"
#include <QTreeView>

class Connection;
class PlaylistModel;

/// View of the current MPD queue (the "now playing" list).
class CurrentPlaylist : public BrowserWidget {
    Q_OBJECT
public:
    explicit CurrentPlaylist(Connection *conn, PlaylistModel *model,
                             QWidget *parent = nullptr);

    QString title() const override { return tr("Current"); }
    QString iconName() const override { return QStringLiteral("media-playback-start"); }

    /// The embedded tree view (for context menu setup).
    QTreeView *treeView() const { return view_; }

private:
    void setupUi();

    Connection *conn_;
    PlaylistModel *model_;
    QTreeView *view_;
};

