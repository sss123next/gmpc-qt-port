/**
 * @file file-system-model.cpp
 * @brief MPD file-system tree model with lazy loading.
 */

#include "file-system-model.h"
#include "core/connection.h"

#include <mpd/entity.h>
#include <mpd/directory.h>
#include <mpd/song.h>
#include <mpd/playlist.h>
#include <mpd/database.h>

FileSystemModel::FileSystemModel(Connection *conn, QObject *parent)
    : QAbstractItemModel(parent)
    , conn_(conn)
    , root_(new FileNode{FileNode::Root, "MPD files", {}, nullptr})
{
}

FileSystemModel::~FileSystemModel()
{
    delete root_;
}

// -- QAbstractItemModel -------------------------------------------------------

QModelIndex FileSystemModel::index(int row, int column,
                                   const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return {};

    auto *p = parent.isValid()
        ? static_cast<FileNode *>(parent.internalPointer())
        : root_;
    if (row < p->children.size())
        return createIndex(row, column, p->children.at(row));
    return {};
}

QModelIndex FileSystemModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return {};

    auto *item = static_cast<FileNode *>(index.internalPointer());
    if (!item->parent || item->parent == root_)
        return {};

    int row = item->parent->parent
        ? item->parent->parent->children.indexOf(item->parent)
        : 0;
    return createIndex(row, 0, item->parent);
}

int FileSystemModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return root_->children.size();
    return static_cast<FileNode *>(parent.internalPointer())->children.size();
}

int FileSystemModel::columnCount(const QModelIndex &) const
{
    return 3; // name, type, duration
}

QVariant FileSystemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    auto *item = static_cast<FileNode *>(index.internalPointer());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0: return item->name;
        case 1: {
            switch (item->type) {
            case FileNode::Directory: return tr("Directory");
            case FileNode::Song:      return tr("Song");
            case FileNode::Playlist:  return tr("Playlist");
            default:                  return {};
            }
        }
        case 2: {
            if (item->type == FileNode::Song && item->duration > 0) {
                int mins = item->duration / 60;
                int secs = item->duration % 60;
                return QStringLiteral("%1:%2")
                    .arg(mins)
                    .arg(secs, 2, 10, QLatin1Char('0'));
            }
            return {};
        }
        }
    }

    if (role == UriRole)
        return item->path;

    return {};
}

QVariant FileSystemModel::headerData(int section, Qt::Orientation orient,
                                     int role) const
{
    if (orient != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case 0: return tr("Name");
    case 1: return tr("Type");
    case 2: return tr("Duration");
    }
    return {};
}

// -- Lazy loading -------------------------------------------------------------

bool FileSystemModel::canFetchMore(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return root_->children.isEmpty();

    auto *node = static_cast<FileNode *>(parent.internalPointer());
    return node->type == FileNode::Directory && !node->loaded;
}

void FileSystemModel::fetchMore(const QModelIndex &parent)
{
    if (!parent.isValid()) {
        if (!root_->loaded)
            loadDirectory(root_);
        return;
    }

    auto *node = static_cast<FileNode *>(parent.internalPointer());
    if (node->type == FileNode::Directory && !node->loaded)
        loadDirectory(node);
}

void FileSystemModel::refresh()
{
    beginResetModel();
    qDeleteAll(root_->children);
    root_->children.clear();
    root_->loaded = false;
    endResetModel();
}

// -- Private ------------------------------------------------------------------

void FileSystemModel::loadDirectory(FileNode *node)
{
    if (!conn_ || !conn_->isConnected())
        return;

    const QByteArray path = node->path.toUtf8();

    // send the listmeta command
    if (!mpd_send_list_meta(conn_->raw(), path.constData()))
        return; // error, bail

    // collect all children into a temp array first, then bulk-insert
    struct Entry { FileNode::Type type; QString name; QString path; unsigned duration; };
    QVector<Entry> entries;

    while (auto *entity = conn_->nextEntity()) {
        switch (mpd_entity_get_type(entity)) {
        case MPD_ENTITY_TYPE_DIRECTORY: {
            auto *dir = mpd_entity_get_directory(entity);
            const QString name = QString::fromUtf8(mpd_directory_get_path(dir));
            const QString full = node->path.isEmpty()
                ? name
                : node->path + QLatin1Char('/') + name;
            entries.append({FileNode::Directory, name, full, 0});
            break;
        }
        case MPD_ENTITY_TYPE_SONG: {
            auto *song = mpd_entity_get_song(entity);
            const QString name = QString::fromUtf8(mpd_song_get_uri(song));
            const QString full = node->path.isEmpty()
                ? name
                : node->path + QLatin1Char('/') + name;
            entries.append({FileNode::Song, name, full,
                            mpd_song_get_duration(song)});
            break;
        }
        case MPD_ENTITY_TYPE_PLAYLIST: {
            auto *pl = mpd_entity_get_playlist(entity);
            const QString name = QString::fromUtf8(mpd_playlist_get_path(pl));
            const QString full = node->path.isEmpty()
                ? name
                : node->path + QLatin1Char('/') + name;
            entries.append({FileNode::Playlist, name, full, 0});
            break;
        }
        default:
            break;
        }
        mpd_entity_free(entity);
    }

    // bulk-insert into model
    if (entries.isEmpty()) {
        node->loaded = true;
        return;
    }

    const int start = node->children.size();
    const QModelIndex idx = node == root_
        ? QModelIndex{}
        : createIndex(node->parent->children.indexOf(node), 0, node);
    beginInsertRows(idx, start, start + entries.size() - 1);
    for (const auto &e : entries) {
        auto *child = new FileNode{e.type, e.name, e.path, node, {}, e.duration, false};
        node->children.append(child);
    }
    endInsertRows();
    node->loaded = true;
}
