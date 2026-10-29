#pragma once

/**
 * @file file-system-model.h
 * @brief QAbstractItemModel for MPD file-system browsing.
 *
 * Lazily loads directory contents via mpd_send_list_meta().
 * Supports three entity types: directory, song, and playlist.
 */

#include <QAbstractItemModel>
#include <QStringList>
#include <QVector>

class Connection;

/// A node in the MPD file-system tree.
struct FileNode {
    enum Type { Directory, Song, Playlist, Root };
    Type type;
    QString name;
    QString path;     ///< full virtual path (empty for root)
    FileNode *parent = nullptr;
    QVector<FileNode *> children;
    bool loaded = false; ///< children fetched from MPD yet

    ~FileNode() { qDeleteAll(children); }
};

/// File-system tree model backed by MPD listmeta commands.
class FileSystemModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit FileSystemModel(Connection *conn, QObject *parent = nullptr);
    ~FileSystemModel() override;

    // -- QAbstractItemModel interface ---------------------------------------
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orient,
                        int role = Qt::DisplayRole) const override;

    // -- Lazy loading ------------------------------------------------------
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

    /// Reload from root.
    void refresh();

private:
    void loadDirectory(FileNode *node);

    Connection *conn_;
    FileNode *root_;
};
