#pragma once

/**
 * @file playlist-editor.h
 * @brief MPD stored-playlist editor.
 *
 * Lists, creates, renames, deletes saved playlists, and shows contents.
 * Двойной клик по позиции списка воспроизводит её (play_path), Delete/Backspace и
 * пункт контекстного меню удаляют позицию из сохранённого списка (`playlistdelete`).
 */

#include "browser-widget.h"

#include <QPoint>
#include <QStringList>

class Connection;
class QListWidget;
class QListWidgetItem;
class MpdCommands;
class QPushButton;

/// Manage MPD stored playlists.
class PlaylistEditor : public BrowserWidget {
    Q_OBJECT
public:
    /** @param conn  соединение с MPD (должно переживать редактор)
     *  @param cmds  команды очереди для активации позиции
     */
    explicit PlaylistEditor(Connection *conn, MpdCommands *cmds,
                            QWidget *parent = nullptr);
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
    /** @brief Двойной клик по позиции списка — воспроизвести этот трэк. */
    void onItemActivated(QListWidgetItem *item);
    /** @brief Удалить выделенную позицию из сохранённого списка (`playlistdelete`). */
    void removeCurrentPosition();

private:
    void refreshPlaylists();
    void refreshContents(const QString &playlistName);
    void updateButtonStates();
    /** @brief Имя выбранного сохранённого списка (пусто, если выбора нет). */
    QString currentPlaylistName() const;
    /** @brief Меню позиции: воспроизвести / удалить из списка. */
    void showContentsMenu(const QPoint &pos);

    Connection *conn_;
    MpdCommands *cmds_;

    QListWidget *playlistList_;
    QListWidget *contentsList_;

    QPushButton *newBtn_;
    QPushButton *renameBtn_;
    QPushButton *deleteBtn_;
    QPushButton *loadBtn_;
};
