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
#include <mpd/client.h>

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
     *  @param conn  An active libmpdclient connection.
     */
    void loadFromConnection(mpd_connection *conn);

    /// Remove all rows.
    void clear();

    // -- lookup -------------------------------------------------------------

    /** @brief Return the song descriptor at a given row (or nullptr). */
    const SongEntry *songAt(int row) const;

    /** @brief Return the queue position for an MPD song id (or -1). */
    int posForId(int id) const;

signals:
    /// Emitted after loadFromConnection() completes.
    void loaded();

private:
    QList<SongEntry> songs_;
};

