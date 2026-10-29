/**
 * @file playlist-model.cpp
 * @brief QAbstractListModel for the MPD queue.
 */

#include "playlist-model.h"
#include "tools/log.h"

PlaylistModel::PlaylistModel(QObject *parent)
    : QAbstractListModel(parent)
{}

int PlaylistModel::rowCount(const QModelIndex &) const
{
    return songs_.size();
}

QVariant PlaylistModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() ||
        index.row() < 0 || index.row() >= songs_.size())
        return {};

    const auto &s = songs_[index.row()];
    switch (role) {
    case IdRole:         return s.id;
    case PositionRole:   return s.pos;
    case ArtistRole:     return s.artist;
    case TitleRole:      return s.title;
    case AlbumRole:      return s.album;
    case DurationRole:   return s.duration;
    case Qt::DisplayRole:
        return QStringLiteral(u"%1 — %2").arg(s.artist, s.title);
    }
    return {};
}

QHash<int, QByteArray> PlaylistModel::roleNames() const
{
    return {
        { IdRole,       "songId"   },
        { PositionRole, "position" },
        { ArtistRole,   "artist"   },
        { TitleRole,    "title"    },
        { AlbumRole,    "album"    },
        { DurationRole, "duration" },
    };
}

void PlaylistModel::loadFromConnection(mpd_connection *conn)
{
    beginResetModel();
    songs_.clear();

    if (!mpd_send_list_queue_meta(conn))
        goto end;

    mpd_entity *entity;
    while ((entity = mpd_recv_entity(conn)) != nullptr) {
        if (mpd_entity_get_type(entity) == MPD_ENTITY_TYPE_SONG) {
            const mpd_song *song = mpd_entity_get_song(entity);
            SongEntry e;
            e.id       = mpd_song_get_id(song);
            e.pos      = mpd_song_get_pos(song);
            e.artist   = QString::fromUtf8(
                mpd_song_get_tag(song, MPD_TAG_ARTIST, 0));
            e.title    = QString::fromUtf8(
                mpd_song_get_tag(song, MPD_TAG_TITLE, 0));
            e.album    = QString::fromUtf8(
                mpd_song_get_tag(song, MPD_TAG_ALBUM, 0));
            e.duration = mpd_song_get_duration(song);
            songs_.append(e);
        }
        mpd_entity_free(entity);
    }
    mpd_response_finish(conn);

end:
    endResetModel();
    emit loaded();
}

void PlaylistModel::clear()
{
    beginResetModel();
    songs_.clear();
    endResetModel();
}

const SongEntry *PlaylistModel::songAt(int row) const
{
    if (row < 0 || row >= songs_.size())
        return nullptr;
    return &songs_[row];
}

int PlaylistModel::posForId(int id) const
{
    for (int i = 0; i < songs_.size(); ++i)
        if (songs_[i].id == id)
            return songs_[i].pos;
    return -1;
}
