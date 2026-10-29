/**
 * @file music-database-model.cpp
 * @brief Tree model for MPD music database — populates on demand.
 *
 * Three-level tree: root → Artist → Album → Track.
 * Uses mpd_search_db_tags for artist/album listing and
 * mpd_search_db_songs + mpd_search_add_tag_constraint for track listing.
 */

#include "music-database-model.h"
#include "core/connection.h"

#include <mpd/search.h>
#include <mpd/tag.h>
#include <mpd/song.h>
#include <mpd/entity.h>

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

// ── Public population API ───────────────────────────────────────────────────

void MusicDatabaseModel::loadArtists()
{
    if (!conn_ || !conn_->isConnected())
        return;

    if (!mpd_search_db_tags(conn_->raw(), MPD_TAG_ARTIST))
        return;

    if (!mpd_search_commit(conn_->raw()))
        return;

    QVector<QString> names;
    while (auto *pair = mpd_recv_pair_tag(conn_->raw(), MPD_TAG_ARTIST)) {
        names.append(QString::fromUtf8(pair->value));
        mpd_return_pair(conn_->raw(), pair);
    }
    mpd_response_finish(conn_->raw());

    // Bulk-insert
    if (names.isEmpty())
        return;

    beginInsertRows({}, 0, names.size() - 1);
    for (const auto &name : names) {
        auto *node = new MusicDbItem{MusicDbItem::Artist, name, {}, root_};
        root_->children.append(node);
    }
    endInsertRows();
}

void MusicDatabaseModel::loadAlbums(const QModelIndex &idx)
{
    if (!idx.isValid())
        return;

    auto *artist = static_cast<MusicDbItem *>(idx.internalPointer());
    if (artist->type != MusicDbItem::Artist)
        return;

    // Check if already loaded
    if (!artist->children.isEmpty())
        return;

    if (!conn_ || !conn_->isConnected())
        return;

    const QByteArray artistName = artist->name.toUtf8();

    // Search tags with constraint
    if (!mpd_search_db_tags(conn_->raw(), MPD_TAG_ALBUM))
        return;

    if (!mpd_search_add_tag_constraint(
            conn_->raw(), MPD_OPERATOR_DEFAULT,
            MPD_TAG_ARTIST, artistName.constData())) {
        mpd_search_cancel(conn_->raw());
        return;
    }

    if (!mpd_search_commit(conn_->raw()))
        return;

    QVector<QString> albums;
    while (auto *pair = mpd_recv_pair_tag(conn_->raw(), MPD_TAG_ALBUM)) {
        albums.append(QString::fromUtf8(pair->value));
        mpd_return_pair(conn_->raw(), pair);
    }
    mpd_response_finish(conn_->raw());

    if (albums.isEmpty())
        return;

    beginInsertRows(idx, 0, albums.size() - 1);
    for (const auto &name : albums) {
        auto *node = new MusicDbItem{MusicDbItem::Album, name, {}, artist};
        artist->children.append(node);
    }
    endInsertRows();
}

void MusicDatabaseModel::loadTracks(const QModelIndex &idx)
{
    if (!idx.isValid())
        return;

    auto *album = static_cast<MusicDbItem *>(idx.internalPointer());
    if (album->type != MusicDbItem::Album)
        return;

    // Check if already loaded
    if (!album->children.isEmpty())
        return;

    auto *artist = album->parent;
    if (!artist || artist->type != MusicDbItem::Artist)
        return;

    if (!conn_ || !conn_->isConnected())
        return;

    const QByteArray artistName = artist->name.toUtf8();
    const QByteArray albumName  = album->name.toUtf8();

    if (!mpd_search_db_songs(conn_->raw(), false))
        return;

    bool ok = true;
    ok = ok && mpd_search_add_tag_constraint(
        conn_->raw(), MPD_OPERATOR_DEFAULT,
        MPD_TAG_ARTIST, artistName.constData());
    ok = ok && mpd_search_add_tag_constraint(
        conn_->raw(), MPD_OPERATOR_DEFAULT,
        MPD_TAG_ALBUM, albumName.constData());
    ok = ok && mpd_search_commit(conn_->raw());

    if (!ok) {
        mpd_search_cancel(conn_->raw());
        return;
    }

    struct TrackEntry { QString title; int duration; };
    QVector<TrackEntry> tracks;

    while (auto *song = mpd_recv_song(conn_->raw())) {
        TrackEntry e;
        e.title = QString::fromUtf8(
            mpd_song_get_tag(song, MPD_TAG_TITLE, 0));
        e.duration = mpd_song_get_duration(song);
        tracks.append(e);
        mpd_song_free(song);
    }
    mpd_response_finish(conn_->raw());

    if (tracks.isEmpty())
        return;

    beginInsertRows(idx, 0, tracks.size() - 1);
    for (const auto &t : tracks) {
        auto *node = new MusicDbItem{
            MusicDbItem::Track, t.title, {}, album};
        album->children.append(node);
    }
    endInsertRows();
}

// ── QAbstractItemModel interface ────────────────────────────────────────────

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

    // Find the row of the parent within its own parent
    MusicDbItem *grandparent = item->parent->parent;
    int row = grandparent
        ? grandparent->children.indexOf(item->parent)
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
    return 2; // name, year
}

QVariant MusicDatabaseModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    auto *item = static_cast<MusicDbItem *>(index.internalPointer());

    if (index.column() == 0 && role == Qt::DisplayRole)
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
