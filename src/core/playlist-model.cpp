/**
 * @file playlist-model.cpp
 * @brief QAbstractListModel for the MPD queue.
 */

#include "playlist-model.h"
#include "core/connection.h"
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

void PlaylistModel::loadFromConnection(Connection &conn)
{
    // Очередь вычитывается ДО сброса модели: consumeError() порождает сигналы,
    // а обработчики не должны застать модель посреди beginResetModel()/
    // endResetModel().
    QList<SongEntry> fetched;

    if (conn.isConnected()) {
        // Деструктор завершает ответ и снимает error-state — раньше ветка
        // неудачного send() уходила через goto, минуя mpd_response_finish().
        Connection::ResponseScope scope(conn, "listqueue");
        mpd_connection *raw = conn.raw();

        if (mpd_send_list_queue_meta(raw)) {
            while (mpd_entity *entity = mpd_recv_entity(raw)) {
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
                    fetched.append(e);
                }
                mpd_entity_free(entity);
            }
        }
    }

    beginResetModel();
    songs_ = std::move(fetched);
    endResetModel();
    emit loaded();
    // The queue may have arrived after the status update that told us which
    // song is playing, so re-resolve and announce it.
    notifyCurrentSong();
}

void PlaylistModel::setSongs(const QList<SongEntry> &songs)
{
    beginResetModel();
    songs_ = songs;
    endResetModel();
    emit loaded();
    notifyCurrentSong();
}

void PlaylistModel::clear()
{
    beginResetModel();
    songs_.clear();
    curId_ = -1;
    curPos_ = -1;
    endResetModel();
    notifyCurrentSong();
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

void PlaylistModel::setCurrentSong(int songId, int songPos)
{
    if (songId == curId_ && songPos == curPos_)
        return;
    curId_ = songId;
    curPos_ = songPos;
    toldCurrent_ = true;
    notifyCurrentSong();
}

const SongEntry *PlaylistModel::currentSong() const
{
    // The id survives queue edits (move/delete shifts positions, never ids),
    // so it is preferred over the position reported by MPD.
    if (curId_ >= 0) {
        for (const auto &s : songs_)
            if (s.id == curId_)
                return &s;
    }
    if (curPos_ >= 0) {
        for (const auto &s : songs_)
            if (s.pos == curPos_)
                return &s;
    }
    return nullptr;
}

void PlaylistModel::notifyCurrentSong()
{
    const SongEntry *song = currentSong();
    const int id = song ? song->id : -1;
    if (id == notifiedId_)
        return;
    // Announcing "nothing plays" before MPD ever named a track is noise: the
    // widgets start empty anyway.
    if (!toldCurrent_ && id < 0)
        return;
    notifiedId_ = id;
    emit currentSongChanged(id);
}
