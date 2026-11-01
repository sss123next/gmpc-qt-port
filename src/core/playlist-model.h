#pragma once

/**
 * @file playlist-model.h
 * @brief QAbstractListModel wrapping the MPD queue (current playlist).
 *
 * Populated by sending mpd_send_list_queue_meta() and iterating
 * with mpd_recv_entity().  Designed to be bound to a CurrentPlaylist
 * QTreeView.
 */

#include <QAbstractListModel>
#include <QList>

class Connection;

/// Lightweight descriptor for one song in the queue.
struct SongEntry {
    int id       = -1;  ///< MPD song id
    int pos      = -1;  ///< position in queue (0-based)
    QString artist;
    QString title;
    QString album;
    int duration = 0;   ///< seconds
};

/// A read-only model of the MPD queue.
class PlaylistModel : public QAbstractListModel {
    Q_OBJECT
public:
    /// Custom data roles exposed to views.
    enum Roles {
        IdRole       = Qt::UserRole + 1,
        PositionRole,
        ArtistRole,
        TitleRole,
        AlbumRole,
        DurationRole
    };

    explicit PlaylistModel(QObject *parent = nullptr);

    // -- QAbstractItemModel interface ---------------------------------------
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // -- population ---------------------------------------------------------

    /** @brief Replace the model content by pulling the queue from MPD.
     *  @param conn  Active connection; must be connected, otherwise no-op.
     *
     *  Ответ завершается на любой ветке: недочитанный список очереди оставил бы
     *  соединение в error-state и все последующие команды встали бы.
     */
    void loadFromConnection(Connection &conn);

    /** @brief Replace the whole queue with @a songs.
     *
     *  Used by incremental updates (plchanges) and by tests that must not talk
     *  to a server; prefer it over re-reading the entire queue.
     */
    void setSongs(const QList<SongEntry> &songs);

    /// Remove all rows.
    void clear();

    // -- lookup -------------------------------------------------------------

    /** @brief Return the song descriptor at a given row (or nullptr). */
    const SongEntry *songAt(int row) const;

    /** @brief Return the queue position for an MPD song id (or -1). */
    int posForId(int id) const;

    // -- current song -------------------------------------------------------

    /** @brief Tell the model which entry MPD reports as currently playing.
     *
     *  Both arguments come from `status`: \a songId is `songid`, \a songPos is
     *  `song`.  The id wins when both are known because positions shift while
     *  the queue is edited; -1 means "unknown".  Nothing is emitted unless the
     *  resolved entry actually changed.
     */
    void setCurrentSong(int songId, int songPos);

    /// Song id reported by MPD for the current track (-1 when unknown).
    int currentSongId() const { return curId_; }
    /// Queue position reported by MPD for the current track (-1 when unknown).
    int currentSongPos() const { return curPos_; }

    /** @brief Return the currently-playing song (or nullptr).
     *
     * Resolved from the id first, then from the queue position.  Returns
     * nullptr while nothing is playing or before MPD has reported a current
     * track — never an arbitrary row of the queue.
     */
    const SongEntry *currentSong() const;

signals:
    /// Emitted after loadFromConnection() completes.
    void loaded();

    /** @brief Emitted when the resolved current song changes.
     *  @param songId  Id of the new current song, -1 when nothing plays.
     */
    void currentSongChanged(int songId);

private:
    /// Emit currentSongChanged() if the resolved entry differs from last time.
    void notifyCurrentSong();

    QList<SongEntry> songs_;
    int curId_ = -1;        ///< `songid` from MPD status
    int curPos_ = -1;       ///< `song` (queue position) from MPD status
    int notifiedId_ = -2;   ///< last id passed to currentSongChanged (-2: none yet)
    /// True once MPD has named a track; before that "no current song" is not a
    /// change worth announcing, it is simply the initial state.
    bool toldCurrent_ = false;
};

