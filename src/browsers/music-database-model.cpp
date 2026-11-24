/**
 * @file music-database-model.cpp
 * @brief Дерево базы MPD: группы → альбомы, объёмы серверным `count … group`.
 *
 * Группы грузит DatabaseBrowser одним запросом на весь уровень; альбомы —
 * canFetchMore()/fetchMore() в момент раскрытия. Трэки живут в правой панели,
 * поэтому третьего уровня здесь нет.
 */

#include "music-database-model.h"
#include "core/connection.h"
#include "tools/log.h"

#include <QCoreApplication>

// ── Подписи и ограничения ─────────────────────────────────────────────────

QVector<MpdTagConstraint> musicDbConstraints(enum mpd_tag_type rootTag,
                                             MusicDbItem::Type type,
                                             const QString &name,
                                             const QString &groupName)
{
    QVector<MpdTagConstraint> constraints;

    switch (type) {
    case MusicDbItem::Group:
        // Ограничение ставится всегда: пустое имя — легальная группа «без тега»,
        // а не отсутствие фильтра (иначе findadd залил бы всю базу).
        constraints.append({rootTag, name});
        break;
    case MusicDbItem::Album:
        // Только альбом по имени собрал бы одноимённые альбомы всех исполнителей.
        if (!groupName.isEmpty() && !name.isEmpty())
            constraints.append({rootTag, groupName});
        if (!name.isEmpty())
            constraints.append({MPD_TAG_ALBUM, name});
        break;
    }

    return constraints;
}

QString compactSpan(quint64 seconds)
{
    if (seconds == 0)
        return {};

    const quint64 hours = seconds / 3600;
    const quint64 minutes = (seconds % 3600) / 60;

    // Округление вверх: «1 s» вместо «0 m» у короткого трэка.
    if (hours > 0)
        return QStringLiteral("%1 h %2 m").arg(hours).arg(minutes);
    if (minutes > 0 || seconds >= 60)
        return QStringLiteral("%1 m").arg((seconds + 59) / 60);
    return QStringLiteral("%1 s").arg(seconds);
}

QString musicDbDisplayName(const QString &raw)
{
    // Пустое значение тега — не ошибка сервера, а файлы без него: подпись нужна,
    // иначе строка выглядит пустой и её невозможно выбрать осмысленно.
    return raw.isEmpty()
        ? QCoreApplication::translate("MusicDatabaseModel", "(Unknown)")
        : raw;
}

// ── Construction ───────────────────────────────────────────────────────────

MusicDatabaseModel::MusicDatabaseModel(Connection *conn, MpdCommands *cmds,
                                       QObject *parent)
    : QAbstractItemModel(parent)
    , conn_(conn)
    , cmds_(cmds)
    , root_(new MusicDbItem{MusicDbItem::Group, "root", -1, 0, {}, nullptr})
{
}

MusicDatabaseModel::~MusicDatabaseModel()
{
    delete root_;
}

// ── Наполнение ─────────────────────────────────────────────────────────────

void MusicDatabaseModel::loadGroups(enum mpd_tag_type tag)
{
    if (!conn_ || !cmds_ || !conn_->isConnected())
        return;

    // Ответ забирается целиком до сброса модели: временная ошибка сервера не
    // должна оставлять пользователя перед пустым экраном.
    const QVector<TagGroupStat> groups = cmds_->groupedCount(tag, {});

    beginResetModel();
    qDeleteAll(root_->children);
    root_->children.clear();
    rootTag_ = tag;
    for (const TagGroupStat &g : groups) {
        auto *item = new MusicDbItem{MusicDbItem::Group, g.value,
                                     g.songs, g.playTimeSec, {}, root_};
        root_->children.append(item);
    }
    endResetModel();
}

void MusicDatabaseModel::clear()
{
    beginResetModel();
    qDeleteAll(root_->children);
    root_->children.clear();
    endResetModel();
}

void MusicDatabaseModel::loadAlbums(const QModelIndex &idx)
{
    if (!idx.isValid() || !conn_ || !cmds_ || !conn_->isConnected())
        return;

    auto *group = static_cast<MusicDbItem *>(idx.internalPointer());
    if (group->type != MusicDbItem::Group || group->loaded)
        return;

    // Ограничение по фактическому тегу группировки: при «Genre» или «Year»
    // запрос по ARTIST не вернул бы ничего.
    const QVector<TagGroupStat> albums =
        cmds_->groupedCount(MPD_TAG_ALBUM, {{rootTag_, group->name}});
    LOG_DEBUG("database: альбомов у группы «{}» — {}", group->name,
              albums.size());

    // Пустой ответ — тоже ответ: иначе узел запрашивали бы при каждом раскрытии,
    // а «детей нет» выглядело бы как «ещё не грузили».
    group->loaded = true;

    if (albums.isEmpty())
        return;

    beginInsertRows(idx, 0, albums.size() - 1);
    for (const TagGroupStat &a : albums) {
        auto *item = new MusicDbItem{MusicDbItem::Album, a.value,
                                     a.songs, a.playTimeSec, {}, group};
        group->children.append(item);
    }
    endInsertRows();
}

QVector<MpdTagConstraint> MusicDatabaseModel::activationFor(
    const QModelIndex &index) const
{
    if (!index.isValid())
        return {};

    auto *item = static_cast<MusicDbItem *>(index.internalPointer());
    const QString groupName = (item->parent && item->parent != root_)
        ? item->parent->name : QString();
    return musicDbConstraints(rootTag_, item->type, item->name, groupName);
}

MusicDbRef MusicDatabaseModel::refFor(const QModelIndex &index) const
{
    if (!index.isValid())
        return {};

    auto *item = static_cast<MusicDbItem *>(index.internalPointer());
    MusicDbRef ref;
    ref.valid       = true;
    ref.type        = item->type;
    ref.name        = item->name;
    ref.songs       = item->songs;
    ref.playTimeSec = item->playTimeSec;
    if (item->parent && item->parent != root_)
        ref.groupName = item->parent->name;
    return ref;
}

// ── Ленивая загрузка альбомов ─────────────────────────────────────────────

bool MusicDatabaseModel::hasChildren(const QModelIndex &parent) const
{
    if (canFetchMore(parent))
        return true;
    return QAbstractItemModel::hasChildren(parent);
}

bool MusicDatabaseModel::canFetchMore(const QModelIndex &parent) const
{
    if (!parent.isValid() || !conn_ || !cmds_ || !conn_->isConnected())
        return false;   // группы грузит DatabaseBrowser::onActivate()

    auto *item = static_cast<MusicDbItem *>(parent.internalPointer());
    return item->type == MusicDbItem::Group && !item->loaded;
}

void MusicDatabaseModel::fetchMore(const QModelIndex &parent)
{
    if (!canFetchMore(parent))
        return;

    loadAlbums(parent);
}

// ── QAbstractItemModel interface ───────────────────────────────────────────

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
    return 3; // имя, объём, распорка
}

QVariant MusicDatabaseModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    auto *item = static_cast<MusicDbItem *>(index.internalPointer());
    const QString name = musicDbDisplayName(item->name);
    const QString span = compactSpan(item->playTimeSec);

    switch (role) {
    case Qt::DisplayRole:
        if (index.column() == 0)
            return name;
        // Вторая колонка — объём: число трэков и время, пока сервер их знает.
        if (index.column() == 1 && item->songs >= 0) {
            const QString count = tr("%n track(s)", "", item->songs);
            return span.isEmpty() ? count : tr("%1 · %2").arg(count, span);
        }
        break;
    case MusicDbRoles::StatsRole:
        if (item->songs >= 0)
            return tr("%n track(s)", "", item->songs) + QLatin1Char(' ') + span;
        break;
    case MusicDbRoles::SortRole:
        if (index.column() == 0)
            return name;
        if (index.column() == 1)
            return item->songs >= 0 ? item->songs : 0;
        break;
    case Qt::ToolTipRole: {
        QString tip = name;
        if (item->songs >= 0)
            tip += QLatin1String("\n")
                 + tr("%n track(s)", "", item->songs);
        if (!span.isEmpty())
            tip += QLatin1Char(' ') + span;
        return tip.trimmed();
    }
    default:
        break;
    }

    return {};
}

QVariant MusicDatabaseModel::headerData(int section, Qt::Orientation orient,
                                        int role) const
{
    if (orient != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case 0: {
        // Заголовок следует способу группировки, а не всегда «Artist».
        const char *tagName = mpd_tag_name(rootTag_);
        return tagName ? QString::fromUtf8(tagName) : tr("Group");
    }
    case 1: return tr("Tracks · Time");
    default: return {};
    }
}