#pragma once

/**
 * @file music-database-model.h
 * @brief Tree model for the MPD music database, grouped by tag.
 *
 * Три уровня: группа верхнего уровня (артист/жанр/год/композитор — см. groupTag())
 * → альбом → трэк. Группа и альбом грузятся лениво через canFetchMore()/fetchMore(),
 * поэтому дерево не вываливает всю базу одним движением.
 */

#include <QAbstractItemModel>
#include <QString>
#include <QList>
#include <QVector>

#include <mpd/tag.h>

#include "core/mpd-commands.h"

class Connection;

/// Custom data roles exposed to views.
namespace MusicDbRoles {
    constexpr int UriRole = Qt::UserRole + 1;
}

/// A node in the music database tree.
struct MusicDbItem {
    /** Уровень строки. Artist = «группа верхнего уровня»: при группировке по жанру,
     *  году или композитору это узел соответствующего тега, а не артист. */
    enum Type { Artist, Album, Track };

    Type type;
    QString name;       ///< artist name, album title, or track title
    QList<MusicDbItem *> children;
    MusicDbItem *parent = nullptr;
    QString uri;        ///< file URI (only for Track)
    bool loaded = false;///< детей запрашивали с сервера (пустой альбом — тоже «загружен»)

    ~MusicDbItem() { qDeleteAll(children); }
};

/** @brief Ограничения `findadd` для строки дерева БД.
 *  @param rootTag   тег группировки верхнего уровня
 *  @param type      уровень строки
 *  @param name      имя строки
 *  @param groupName имя родительской группы (для альбома), пусто для группы и трэка
 *
 *  Группа → `(rootTag, имя)`; альбом → `(rootTag, группа) + (ALBUM, имя)`; трэк →
 *  пусто: у него есть собственный URI, его играет `MpdCommands::activateUri`.
 */
[[nodiscard]] QVector<MpdTagConstraint> musicDbConstraints(
    enum mpd_tag_type rootTag, MusicDbItem::Type type,
    const QString &name, const QString &groupName);

/// Намерение при активации строки дерева (двойной клик).
struct MusicDbActivation {
    QString uri;                              ///< трэк: воспроизвести его
    QVector<MpdTagConstraint> constraints;    ///< группа/альбом: добавить всё подходящее
};

/// Tree model for browsing the MPD music database.
class MusicDatabaseModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit MusicDatabaseModel(Connection *conn, QObject *parent = nullptr);
    ~MusicDatabaseModel() override;

    // ── Population ────────────────────────────────────────────────────────
    /** @brief Загрузить группы верхнего уровня по тегу (артисты — частный случай).
     *  Модель перезаполняется целиком: повторный вызов не должен плодить дубли.
     */
    void loadByTag(enum mpd_tag_type tag);

    /// Unload all data and free memory.
    void clear();

    /// Загрузить группы верхнего уровня, сгруппированные по артисту.
    void loadArtists() { loadByTag(MPD_TAG_ARTIST); }

    /// Тег группировки верхнего уровня — нужен ограничениям `findadd`.
    enum mpd_tag_type groupTag() const { return rootTag_; }

    /** @brief Что делать, когда пользователь активировал строку дерева. */
    MusicDbActivation activationFor(const QModelIndex &index) const;

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

    // ── Ленивая загрузка альбомов и трэков ────────────────────────────────
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

private:
    Connection *conn_;
    MusicDbItem *root_;               ///< invisible root
    enum mpd_tag_type rootTag_ = MPD_TAG_ARTIST; ///< тег групп верхнего уровня
};
