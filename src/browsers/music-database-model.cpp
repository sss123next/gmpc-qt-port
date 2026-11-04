/**
 * @file music-database-model.cpp
 * @brief Tree model for MPD music database — populates on demand.
 *
 * Три уровня: группа верхнего уровня (артист/жанр/год/композитор) → альбом → трэк.
 * Группы грузит DatabaseBrowser, альбомы и трэки — canFetchMore()/fetchMore(), то
 * есть в момент раскрытия узла.
 */

#include "music-database-model.h"
#include "core/connection.h"

#include <mpd/search.h>
#include <mpd/tag.h>
#include <mpd/song.h>
#include <mpd/entity.h>

// ── Активация строки ────────────────────────────────────────────────────────

QVector<MpdTagConstraint> musicDbConstraints(enum mpd_tag_type rootTag,
                                             MusicDbItem::Type type,
                                             const QString &name,
                                             const QString &groupName)
{
    QVector<MpdTagConstraint> constraints;

    switch (type) {
    case MusicDbItem::Artist:   // группа верхнего уровня: артист/жанр/год/композитор
        if (!name.isEmpty())
            constraints.append({rootTag, name});
        break;
    case MusicDbItem::Album:
        // Только альбом по имени собрал бы одноимённые альбомы всех исполнителей.
        if (!groupName.isEmpty() && !name.isEmpty())
            constraints.append({rootTag, groupName});
        if (!name.isEmpty())
            constraints.append({MPD_TAG_ALBUM, name});
        break;
    case MusicDbItem::Track:
        // У трэка есть URI — его играет MpdCommands::activateUri.
        break;
    }

    return constraints;
}

// ── Construction ────────────────────────────────────────────────────────────

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

void MusicDatabaseModel::loadByTag(enum mpd_tag_type tag)
{
    if (!conn_ || !conn_->isConnected())
        return;

    QVector<QString> names;
    {
        Connection::ResponseScope scope(*conn_, "listtags");

        if (!mpd_search_db_tags(conn_->raw(), tag))
            return;

        if (!mpd_search_commit(conn_->raw()))
            return;

        while (auto *pair = mpd_recv_pair_tag(conn_->raw(), tag)) {
            names.append(QString::fromUtf8(pair->value));
            mpd_return_pair(conn_->raw(), pair);
        }
    }

    // Ответ забран целиком — и только теперь меняется дерево. Раньше модель
    // очищалась до запроса, поэтому временная ошибка сервера оставляла пользователя
    // с пустым экраном вместо прежнего списка.
    beginResetModel();
    qDeleteAll(root_->children);
    root_->children.clear();
    rootTag_ = tag;
    for (const auto &name : names)
        root_->children.append(new MusicDbItem{MusicDbItem::Artist, name, {}, root_});
    endResetModel();
}

void MusicDatabaseModel::loadAlbums(const QModelIndex &idx)
{
    if (!idx.isValid())
        return;

    auto *group = static_cast<MusicDbItem *>(idx.internalPointer());
    if (group->type != MusicDbItem::Artist || group->loaded)
        return;

    if (!conn_ || !conn_->isConnected())
        return;

    QVector<QString> albums;
    {
        Connection::ResponseScope scope(*conn_, "listalbums");

        const QByteArray groupName = group->name.toUtf8();

        // Ограничение по фактическому тегу группировки: при «Genre» или «Year» на
        // верхнем уровне запрос по ARTIST не вернул бы ничего.
        if (!mpd_search_db_tags(conn_->raw(), MPD_TAG_ALBUM))
            return;

        bool ok = mpd_search_add_tag_constraint(
            conn_->raw(), MPD_OPERATOR_DEFAULT, rootTag_, groupName.constData());
        ok = ok && mpd_search_commit(conn_->raw());

        if (!ok) {
            mpd_search_cancel(conn_->raw());
            return;
        }

        while (auto *pair = mpd_recv_pair_tag(conn_->raw(), MPD_TAG_ALBUM)) {
            albums.append(QString::fromUtf8(pair->value));
            mpd_return_pair(conn_->raw(), pair);
        }
    }

    // Пустой ответ — тоже ответ: без этого флага узел запрашивали бы при каждом
    // раскрытии, а «детей нет» выглядело бы как «ещё не грузили».
    group->loaded = true;

    if (albums.isEmpty())
        return;

    beginInsertRows(idx, 0, albums.size() - 1);
    for (const auto &name : albums)
        group->children.append(new MusicDbItem{MusicDbItem::Album, name, {}, group});
    endInsertRows();
}

void MusicDatabaseModel::loadTracks(const QModelIndex &idx)
{
    if (!idx.isValid())
        return;

    auto *album = static_cast<MusicDbItem *>(idx.internalPointer());
    if (album->type != MusicDbItem::Album || album->loaded)
        return;

    auto *group = album->parent;
    if (!group || group->type != MusicDbItem::Artist)
        return;

    if (!conn_ || !conn_->isConnected())
        return;

    struct TrackEntry { QString title; QString uri; };
    QVector<TrackEntry> tracks;
    {
        Connection::ResponseScope scope(*conn_, "searchalbum");

        const QByteArray groupName = group->name.toUtf8();
        const QByteArray albumName = album->name.toUtf8();

        // Точное совпадение: подстрока притянула бы в альбом чужие трэки.
        if (!mpd_search_db_songs(conn_->raw(), true))
            return;

        bool ok = mpd_search_add_tag_constraint(
            conn_->raw(), MPD_OPERATOR_DEFAULT, rootTag_, groupName.constData());
        ok = ok && mpd_search_add_tag_constraint(
            conn_->raw(), MPD_OPERATOR_DEFAULT, MPD_TAG_ALBUM, albumName.constData());
        ok = ok && mpd_search_commit(conn_->raw());

        if (!ok) {
            mpd_search_cancel(conn_->raw());
            return;
        }

        while (auto *song = mpd_recv_song(conn_->raw())) {
            TrackEntry e;
            e.title = QString::fromUtf8(mpd_song_get_tag(song, MPD_TAG_TITLE, 0));
            e.uri = QString::fromUtf8(mpd_song_get_uri(song));
            tracks.append(e);
            mpd_song_free(song);
        }
    }

    album->loaded = true;

    if (tracks.isEmpty())
        return;

    beginInsertRows(idx, 0, tracks.size() - 1);
    for (const auto &t : tracks) {
        auto *node = new MusicDbItem{MusicDbItem::Track, t.title, {}, album};
        node->uri = t.uri;
        album->children.append(node);
    }
    endInsertRows();
}

MusicDbActivation MusicDatabaseModel::activationFor(const QModelIndex &index) const
{
    MusicDbActivation activation;
    if (!index.isValid())
        return activation;

    auto *item = static_cast<MusicDbItem *>(index.internalPointer());
    if (item->type == MusicDbItem::Track) {
        activation.uri = item->uri;
        return activation;
    }

    const QString groupName = (item->parent && item->parent != root_)
        ? item->parent->name : QString();
    activation.constraints = musicDbConstraints(rootTag_, item->type,
                                                item->name, groupName);
    return activation;
}

// ── Ленивая загрузка альбомов и трэков ──────────────────────────────────────

bool MusicDatabaseModel::canFetchMore(const QModelIndex &parent) const
{
    if (!parent.isValid() || !conn_ || !conn_->isConnected())
        return false;   // группы грузит DatabaseBrowser::onActivate()

    auto *item = static_cast<MusicDbItem *>(parent.internalPointer());
    return item->type != MusicDbItem::Track && !item->loaded;
}

void MusicDatabaseModel::fetchMore(const QModelIndex &parent)
{
    if (!canFetchMore(parent))
        return;

    auto *item = static_cast<MusicDbItem *>(parent.internalPointer());
    if (item->type == MusicDbItem::Artist)
        loadAlbums(parent);
    else
        loadTracks(parent);
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

    if (role == MusicDbRoles::UriRole && item->type == MusicDbItem::Track)
        return item->uri;

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
