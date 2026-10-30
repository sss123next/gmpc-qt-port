#pragma once

/**
 * @file music-database-model.h
 * @brief Tree model for the MPD music database (artist / album).
 *
 * Top-level items are artists; each artist has child album items.
 * Albums have leaf track entries (future).
 *
 * Populated by calling loadArtists() which sends MPD commands via
 * Connection.
 */

#include <QAbstractItemModel>
#include <QString>
#include <QList>

#include <mpd/tag.h>

class Connection;

/// Custom data roles exposed to views.
namespace MusicDbRoles {
    constexpr int UriRole = Qt::UserRole + 1;
}

/// A node in the music database tree.
struct MusicDbItem {
    enum Type { Artist, Album, Track };

    Type type;
    QString name;       ///< artist name, album title, or track title
    QList<MusicDbItem *> children;
    MusicDbItem *parent = nullptr;
    QString uri;        ///< file URI (only for Track)

    ~MusicDbItem() { qDeleteAll(children); }
};

/// Tree model for browsing the MPD music database.
class MusicDatabaseModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit MusicDatabaseModel(Connection *conn, QObject *parent = nullptr);
    ~MusicDatabaseModel() override;

    // ── Population ────────────────────────────────────────────────────────
    /** Load the list of artists from MPD. */
    void loadArtists();

    /**
     * Load top-level items grouped by the given tag type.
     * Replaces loadArtists() when the user switches tag groups.
     */
    void loadByTag(enum mpd_tag_type tag);

    /**
     * Load albums for a given artist node.
     * @param idx model index of the artist
     */
    void loadAlbums(const QModelIndex &idx);

    /**
     * Load tracks for a given (artist, album) node.
     * @param idx model index of the album
     */
    void loadTracks(const QModelIndex &idx);

    // ── QAbstractItemModel interface ──────────────────────────────────────
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    Connection *conn_;
    MusicDbItem *root_; ///< invisible root
};

