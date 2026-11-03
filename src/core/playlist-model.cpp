/**
 * @file playlist-model.cpp
 * @brief QAbstractTableModel for the MPD queue.
 */

#include "playlist-model.h"
#include "core/connection.h"
#include "tools/log.h"

#include <QMimeData>
#include <QSet>

#include <algorithm>
#include <functional>

namespace {

/// Длительность в виде mm:ss (или h:mm:ss для длинных записей).
QString formatDuration(int seconds)
{
    if (seconds <= 0)
        return QStringLiteral("--:--");   // неизвестная длительность — не «0:00»

    const int s = seconds % 60;
    const int m = (seconds / 60) % 60;
    const int h = seconds / 3600;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

/// Имя файла из URI — чем показать трек, у которого нет тега Title.
QString fileNameOf(const QString &uri)
{
    const int slash = uri.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? uri.mid(slash + 1) : uri;
}

/// Заголовок строки: тег Title, а если его нет — имя файла.
QString displayTitle(const SongEntry &s)
{
    if (!s.title.isEmpty())
        return s.title;
    return fileNameOf(s.uri);
}

} // namespace

PlaylistModel::PlaylistModel(QObject *parent)
    : QAbstractTableModel(parent)
{}

int PlaylistModel::rowCount(const QModelIndex &) const
{
    return songs_.size();
}

int PlaylistModel::columnCount(const QModelIndex &) const
{
    return ColumnCount;
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
    case UriRole:        return s.uri;
    case Qt::DisplayRole:
        // Колонки, а не одна «artist — title»: сортировка и фильтр работают по
        // каждой колонке отдельно.
        switch (index.column()) {
        case TitleColumn:  return displayTitle(s);
        case ArtistColumn: return s.artist;
        case AlbumColumn:  return s.album;
        case TimeColumn:   return formatDuration(s.duration);
        }
        return {};
    case Qt::TextAlignmentRole:
        if (index.column() == TimeColumn)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        return {};
    case SortRole:
        // Сортировка по DisplayRole сравнивала бы «10:00» и «9:00» как текст, из-за
        // чего десятиминутный трек встал бы раньше девятисекундного.
        switch (index.column()) {
        case TitleColumn:  return displayTitle(s);
        case ArtistColumn: return s.artist;
        case AlbumColumn:  return s.album;
        case TimeColumn:   return s.duration;
        }
        return {};
    }
    return {};
}

QVariant PlaylistModel::headerData(int section, Qt::Orientation orientation,
                                   int role) const
{
    if (role != Qt::DisplayRole)
        return {};

    if (orientation == Qt::Vertical)
        return section + 1;   // вертикальную шапку вьюха по умолчанию прячет

    switch (section) {
    case TitleColumn:  return tr("Title");
    case ArtistColumn: return tr("Artist");
    case AlbumColumn:  return tr("Album");
    case TimeColumn:   return tr("Time");
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
        { UriRole,      "uri"      },
    };
}

// ── Перетаскивание строк ─────────────────────────────────────────────────────

const QString &PlaylistModel::songIdMimeType()
{
    static const QString type = QStringLiteral("application/x-gmpc-qt-songid");
    return type;
}

QList<int> PlaylistModel::songIdsFromMimeData(const QMimeData *data)
{
    QList<int> ids;
    if (!data || !data->hasFormat(songIdMimeType()))
        return ids;

    const QByteArray raw = data->data(songIdMimeType());
    for (const QByteArray &part : raw.split(',')) {
        bool ok = false;
        const int id = part.toInt(&ok);
        if (ok && id >= 0)
            ids.append(id);
    }
    return ids;
}

Qt::ItemFlags PlaylistModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.isValid())
        f |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;   // перестановка внутри очереди
    return f;
}

QMimeData *PlaylistModel::mimeData(const QModelIndexList &indexes) const
{
    auto *data = new QMimeData;

    QByteArray ids;
    QStringList names;
    for (const QModelIndex &index : indexes) {
        // Строка попадает в список один раз, независимо от числа выделенных колонок.
        if (!index.isValid() || index.column() != TitleColumn)
            continue;
        const SongEntry *s = songAt(index.row());
        if (!s || s->id < 0)
            continue;
        if (!ids.isEmpty())
            ids.append(',');
        ids.append(QByteArray::number(s->id));
        names << displayTitle(*s);
    }

    data->setData(songIdMimeType(), ids);
    // Обычный текст — чтобы перетаскивание в стороннее приложение не приносило
    // пустоту вместо названий.
    data->setText(names.join(QLatin1Char('\n')));
    return data;
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
                    e.uri      = QString::fromUtf8(mpd_song_get_uri(song));
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

int PlaylistModel::targetPosForMove(int sourceRow, int count, int destRow, int queueSize)
{
    if (sourceRow < 0 || count <= 0 || sourceRow + count > queueSize)
        return -1;

    // -1 — в конец очереди; иначе «перед строкой destRow» в координатах ДО перемещения.
    // Значение за концом очереди ограничиваем хвостом: дальше двигать некуда.
    const int target = qMin(destRow < 0 ? queueSize : destRow, queueSize);
    if (target == sourceRow || (target > sourceRow && target <= sourceRow + count))
        return -1;   // смещения нет: строки уже стоят на этом месте

    // MPD считает TO по очереди без переносимых строк, поэтому при движении вниз
    // итоговая позиция на длину блока меньше экранной.
    return target > sourceRow ? target - count : target;
}

bool PlaylistModel::moveRowsWithin(int sourceRow, int count, int destRow)
{
    const int toPos = targetPosForMove(sourceRow, count, destRow, songs_.size());
    if (toPos < 0)
        return false;

    // Qt ждёт destinationChild в координатах исходной модели и сам вычитает число
    // снимаемых строк при движении вниз. Проверка выше обязательна: внутри у
    // beginMoveRows стоит Q_ASSERT, а не возврат false.
    const int destinationChild = toPos > sourceRow ? toPos + count : toPos;
    if (!beginMoveRows(QModelIndex(), sourceRow, sourceRow + count - 1,
                       QModelIndex(), destinationChild))
        return false;

    const QList<SongEntry> moved = songs_.mid(sourceRow, count);
    for (int i = 0; i < count; ++i)
        songs_.removeAt(sourceRow);
    for (int i = 0; i < moved.size(); ++i)
        songs_.insert(toPos + i, moved[i]);

    endMoveRows();
    renumberPositions();
    return true;
}

void PlaylistModel::removeRowsAt(const QList<int> &rows)
{
    QSet<int> unique;
    for (int row : rows)
        if (row >= 0 && row < songs_.size())
            unique.insert(row);
    if (unique.isEmpty())
        return;

    const int playing = currentSong() ? currentSong()->id : -1;

    // Снизу вверх: индексы строк выше уже удалённого блока не сдвигаются, поэтому
    // каждое извещение остаётся согласованным с текущим состоянием модели.
    QList<int> sorted = unique.values();
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());

    int i = 0;
    while (i < sorted.size()) {
        const int bottom = sorted[i];
        int top = bottom;
        ++i;
        while (i < sorted.size() && sorted[i] == top - 1) {   // смежная строка — тот же блок
            top = sorted[i];
            ++i;
        }
        beginRemoveRows(QModelIndex(), top, bottom);
        for (int row = bottom; row >= top; --row)
            songs_.removeAt(row);
        endRemoveRows();
    }

    renumberPositions();

    // Удалённый трек больше не текущий. Позиция из `status` после удаления враёт
    // (строки сдвинулись), а разрешать по ней — значит объявить играющим соседний
    // трек, которого сервер ещё не начинал. Снимаем позицию: `currentSong()` вернёт
    // nullptr, а следующий status (полсекунды, при живом слушателе — сразу) назовёт
    // настоящий.
    if (playing >= 0 && !std::any_of(songs_.begin(), songs_.end(),
                                     [playing](const SongEntry &s) { return s.id == playing; }))
        curPos_ = -1;

    notifyCurrentSong();
}

void PlaylistModel::renumberPositions()
{
    // В полном списке очереди pos == номер строки; после перемещения или удаления
    // иначе позиции расходились бы с тем, что через plchanges пришлёт сервер.
    for (int i = 0; i < songs_.size(); ++i)
        songs_[i].pos = i;
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
