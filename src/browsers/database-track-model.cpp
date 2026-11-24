/**
 * @file database-track-model.cpp
 * @brief Таблица трэков правой панели Database.
 */

#include "database-track-model.h"

#include <QFileInfo>

#include <algorithm>
#include <numeric>

namespace {

/// Первое целое в строке («3/12» → 3); нечисловое — большое число, чтобы уйти в конец.
int leadingNumber(const QString &text)
{
    int value = 0;
    bool any = false;
    for (const QChar c : text) {
        if (!c.isDigit())
            break;
        any = true;
        value = value * 10 + c.digitValue();
    }
    return any ? value : INT_MAX;
}

QString durationText(int seconds)
{
    if (seconds <= 0)
        return {};
    return QStringLiteral("%1:%2")
        .arg(seconds / 60)
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

DatabaseTrackModel::DatabaseTrackModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int DatabaseTrackModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : tracks_.size();
}

int DatabaseTrackModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant DatabaseTrackModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= tracks_.size())
        return {};

    const DbSong &t = tracks_.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case TrackColumn:  return t.track;
        // Пустое название — не повод для пустой строки: путь различим и кликабелен.
        case TitleColumn:  return t.title.isEmpty() ? QFileInfo(t.uri).fileName() : t.title;
        case TimeColumn:   return durationText(t.duration);
        case ArtistColumn: return t.artist;
        case AlbumColumn:  return t.album;
        case DiscColumn:   return t.disc;
        default:           return {};
        }
    case SortRole:
        switch (index.column()) {
        case TrackColumn: return leadingNumber(t.track);
        case TimeColumn:  return t.duration;
        case DiscColumn:  return leadingNumber(t.disc);
        default:          break;
        }
        // Для текста — тот же текст, что на экране (регистр не должен влиять).
        return data(index, Qt::DisplayRole).toString().toCaseFolded();
    case UriRole:
        return t.uri;
    case Qt::ToolTipRole: {
        QString tip = t.title.isEmpty() ? QFileInfo(t.uri).fileName() : t.title;
        if (!t.artist.isEmpty())
            tip += QLatin1String("\n") + t.artist;
        if (!t.album.isEmpty())
            tip += QLatin1String(" — ") + t.album;
        return tip;
    }
    default:
        break;
    }

    return {};
}

QVariant DatabaseTrackModel::headerData(int section, Qt::Orientation orient,
                                         int role) const
{
    if (orient != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case TrackColumn:  return tr("#");
    case TitleColumn:  return tr("Title");
    case TimeColumn:   return tr("Time");
    case ArtistColumn: return tr("Artist");
    case AlbumColumn:  return tr("Album");
    case DiscColumn:   return tr("Disc");
    default:           return {};
    }
}

void DatabaseTrackModel::sort(int column, Qt::SortOrder order)
{
    if (column < 0 || column >= ColumnCount || tracks_.size() < 2)
        return;

    QVector<int> rows(tracks_.size());
    std::iota(rows.begin(), rows.end(), 0);

    const auto less = [this, column](int a, int b) {
        const QVariant va = data(index(a, column), SortRole);
        const QVariant vb = data(index(b, column), SortRole);
        if (va.userType() == QMetaType::Int && vb.userType() == QMetaType::Int)
            return va.toInt() < vb.toInt();
        return QString::compare(va.toString(), vb.toString()) < 0;
    };

    std::stable_sort(rows.begin(), rows.end(),
                     [&less, order](int a, int b) {
                         return order == Qt::AscendingOrder ? less(a, b) : less(b, a);
                     });

    // Старая строка → новая: персистентные индексы обязаны переехать вместе
    // со своими трэками, иначе выделение после сортировки указывает не туда.
    QVector<int> newRowOf(tracks_.size());
    for (int i = 0; i < rows.size(); ++i)
        newRowOf[rows.at(i)] = i;

    const auto oldIndexes = persistentIndexList();
    emit layoutAboutToBeChanged({}, VerticalSortHint);

    QVector<DbSong> sorted;
    sorted.reserve(tracks_.size());
    for (const int row : rows)
        sorted.append(tracks_.at(row));
    tracks_ = std::move(sorted);

    for (const QModelIndex &old : oldIndexes) {
        const QModelIndex fresh =
            createIndex(newRowOf.at(old.row()), old.column(), nullptr);
        changePersistentIndex(old, fresh);
    }

    emit layoutChanged({}, VerticalSortHint);
}

void DatabaseTrackModel::setTracks(const QVector<DbSong> &tracks)
{
    beginResetModel();
    tracks_ = tracks;
    endResetModel();
}

void DatabaseTrackModel::clear()
{
    setTracks({});
}

const DbSong *DatabaseTrackModel::trackAt(int row) const
{
    return (row >= 0 && row < tracks_.size()) ? &tracks_.at(row) : nullptr;
}

QString DatabaseTrackModel::columnKey(int column)
{
    switch (column) {
    case TrackColumn:  return QStringLiteral("track");
    case TitleColumn:  return QStringLiteral("title");
    case TimeColumn:   return QStringLiteral("time");
    case ArtistColumn: return QStringLiteral("artist");
    case AlbumColumn:  return QStringLiteral("album");
    case DiscColumn:   return QStringLiteral("disc");
    default:           return {};
    }
}

int DatabaseTrackModel::columnForKey(const QString &key)
{
    for (int c = 0; c < ColumnCount; ++c) {
        if (columnKey(c) == key)
            return c;
    }
    return -1;
}