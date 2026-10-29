/**
 * @file music-database-model.cpp
 * @brief Tree model for MPD music database — initial skeleton.
 *
 * TODO: full implementation with MPD tag search iteration.
 */

#include "music-database-model.h"
#include "core/connection.h"

MusicDatabaseModel::MusicDatabaseModel(Connection *conn, QObject *parent)
    : QAbstractItemModel(parent)
    , conn_(conn)
    , root_(new MusicDbItem{MusicDbItem::Artist, "root", {}, nullptr})
{
}

MusicDatabaseModel::~MusicDatabaseModel()
{
    delete root_;
}

void MusicDatabaseModel::loadArtists()
{
    // TODO: mpd_search_db_tags(...), iterate, beginInsertRows/endInsertRows
}

QModelIndex MusicDatabaseModel::index(int row, int column,
                                      const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return {};

    MusicDbItem *p = parent.isValid()
        ? static_cast<MusicDbItem *>(parent.internalPointer())
        : root_;

    if (row < p->children.size())
        return createIndex(row, column, p->children.at(row));

    return {};
}

QModelIndex MusicDatabaseModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return {};

    auto *item = static_cast<MusicDbItem *>(index.internalPointer());
    if (!item->parent || item->parent == root_)
        return {};

    int row = item->parent->parent
        ? item->parent->parent->children.indexOf(item->parent)
        : 0;
    return createIndex(row, 0, item->parent);
}

int MusicDatabaseModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return root_->children.size();

    auto *item = static_cast<MusicDbItem *>(parent.internalPointer());
    return item->children.size();
}

int MusicDatabaseModel::columnCount(const QModelIndex &) const
{
    return 2; // artist/album name, year
}

QVariant MusicDatabaseModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || role != Qt::DisplayRole)
        return {};

    auto *item = static_cast<MusicDbItem *>(index.internalPointer());
    if (index.column() == 0)
        return item->name;
    return {};
}

QVariant MusicDatabaseModel::headerData(int section, Qt::Orientation orient,
                                        int role) const
{
    if (orient != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case 0: return tr("Artist / Album");
    case 1: return tr("Year");
    default: return {};
    }
}
