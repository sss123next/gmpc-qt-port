#pragma once

/**
 * @file playlist-editor.h
 * @brief MPD stored-playlist editor.
 *
 * Lists, creates, renames, deletes saved playlists, and shows contents.
 */

#include "browser-widget.h"

#include <QStringList>

class Connection;
class QListWidget;
class QPushButton;

/// Manage MPD stored playlists.
class PlaylistEditor : public BrowserWidget {
    Q_OBJECT
public:
    explicit PlaylistEditor(Connection *conn, QWidget *parent = nullptr);
    ~PlaylistEditor() override = default;

    // BrowserWidget interface
    void onActivate() override { refreshPlaylists(); }
    void onDeactivate() override {}
    void onConnectionChanged(bool connected) override;
    QString title() const override { return tr("Playlists"); }
    QString iconName() const override { return QStringLiteral("media-playlist"); }

private slots:
    void onPlaylistSelected(int row);
    void onNewPlaylist();
    void onRenamePlaylist();
    void onDeletePlaylist();
    void onLoadPlaylist();

private:
    void refreshPlaylists();
    void refreshContents(const QString &playlistName);
    void updateButtonStates();

    Connection *conn_;

    QListWidget *playlistList_;
    QListWidget *contentsList_;

    QPushButton *newBtn_;
    QPushButton *renameBtn_;
    QPushButton *deleteBtn_;
    QPushButton *loadBtn_;
};
