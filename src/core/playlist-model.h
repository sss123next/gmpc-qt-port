#pragma once

/**
 * @file playlist-model.h
 * @brief QAbstractTableModel wrapping the MPD queue (current playlist).
 *
 * Содержимое подкладывает QueueSync (core/queue-sync.h): он читает очередь в своём
 * потоке и присылает либо весь список, либо теги изменившихся строк. Вьюха —
 * CurrentPlaylist.
 */

#include <QAbstractTableModel>
#include <QList>

class QMimeData;

/// Lightweight descriptor for one song in the queue.
struct SongEntry {
    int id       = -1;  ///< MPD song id
    int pos      = -1;  ///< position in queue (0-based)
    QString artist;
    QString title;
    QString album;
    QString uri;        ///< file name/URI — fallback when there is no Title tag
    int duration = 0;   ///< seconds
};

struct mpd_song;

/** @brief Ключ колонки для хранения её настроек (порядок/видимость).
 *
 *  Именем, а не номером: пользователь переставляет колонки местами, и настройка
 *  обязана остаться при своей колонке — так же поступал оригинал
 *  (`eggcolumnmodel.c` хранил видимость по имени столбца).
 */
[[nodiscard]] QString playlistColumnKey(int column);

/** @brief Колонка по её ключу (-1, если такого ключа нет). */
int playlistColumnForKey(const QString &key);

/** @brief Развернуть форматную строку трэка (аналог `mpc --format`).
 *
 *  Подстановки: `$title`, `$artist`, `$album`, `$duration`, `$file`. Неизвестный
 *  токен остаётся в тексте как есть, отсутствующий тег даёт пустую строку.
 */
[[nodiscard]] QString expandSongMarkup(const QString &markup, const SongEntry &song);

/** @brief Разбор одной записи ответа MPD в SongEntry.
 *  Общий для `listqueue` и `plchanges`: теги у этих ответов одинаковые, а две
 *  копии разбора означали бы, что очередь и дифф наполняются разными полями. */
[[nodiscard]] SongEntry songEntryFromMpd(const struct mpd_song *song);

/// A model of the MPD queue: four columns, one row per queued song.
class PlaylistModel : public QAbstractTableModel {
    Q_OBJECT
public:
    /** @brief Columns shown by the current-playlist view.
     *
     *  Позиция очереди колонкой не является: она и есть номер строки (см.
     *  renumberPositions()), а показывать её отдельным столбцом оригинальный
     *  gmpc не пробовал.
     */
    enum Column {
        TitleColumn = 0,
        ArtistColumn,
        AlbumColumn,
        TimeColumn,
        ColumnCount
    };

    /// Custom data roles exposed to views.
    enum Roles {
        IdRole       = Qt::UserRole + 1,
        PositionRole,
        ArtistRole,
        TitleRole,
        AlbumRole,
        DurationRole,
        UriRole,
        /** @brief Роль сортировки: длительность сравнивается численно, а не как
         *  «10:00» < «9:00»; для текстовых колонок — тот же текст, что и на экране. */
        SortRole
    };

    explicit PlaylistModel(QObject *parent = nullptr);

    // -- QAbstractItemModel interface ---------------------------------------
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // -- перетаскивание строк -----------------------------------------------

    /** @brief MIME-тип строки очереди: songid через запятую.
     *  Собственный тип вместо внутреннего формата Qt: разбирать его приходится
     *  нам же, и формат этот не документирован.
     */
    static const QString &songIdMimeType();

    /// Идентификаторы из перетаскиваемых данных (пусто, если это не наши данные).
    static QList<int> songIdsFromMimeData(const QMimeData *data);

    Qt::ItemFlags flags(const QModelIndex &index) const override;
    Qt::DropActions supportedDragActions() const override { return Qt::MoveAction; }
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }
    QMimeData *mimeData(const QModelIndexList &indexes) const override;

    // -- population ---------------------------------------------------------

    /** @brief Заменить всё содержимое модели списком @a songs.
     *
     *  Используется синхронизатором очереди (QueueSync) и тестами, которым не нужен
     *  сервер; очередь модель сама не читает — `listqueue` на 65 тыс. позиций это
     *  ~914 мс, а в GUI-потоке такие чтения означают зависшее окно.
     */
    void setSongs(const QList<SongEntry> &songs);

    /// Remove all rows.
    void clear();

    // -- локальное отражение изменений очереди -------------------------------

    /** @brief Переместить строки внутри модели, выполнив команду серверу.
     *  @param sourceRow Первая перемещаемая строка.
     *  @param count     Сколько строк (соседних).
     *  @param destRow   Индекс строки, ПЕРЕД которой встанут перенесённые
     *                   (-1 — в конец очереди).
     *  @returns false, если диапазон выходит за пределы модели или смещения нет.
     *
     *  Модель не является источником истины об очереди: порядок меняет MPD, а здесь
     *  отражается уже успешная команда — иначе при отказе сервера UI разошёлся бы с
     *  очередью. Позиции после перемещения перенумеровываются: в полном списке
     *  очереди `pos` совпадает с номером строки.
     */
    bool moveRowsWithin(int sourceRow, int count, int destRow);

    /** @brief Аргумент TO команды `move_range` для того же перемещения.
     *  @param sourceRow Первая строка блока в исходной модели.
     *  @param count     Длина блока.
     *  @param destRow   Строка, ПЕРЕД которой встанет блок (-1 — в конец),
     *                   в координатах ДО перемещения.
     *  @param queueSize  Длина очереди.
     *  @returns позиция первой строки блока после перемещения или -1, если
     *           перемещения нет (блок уже там) либо он за пределами очереди.
     *
     *  Координаты расходятся: MPD считает TO по очереди, из которой переносимые
     *  строки уже вынуты, а интерфейс — по той, что пользователь видит на экране.
     */
    static int targetPosForMove(int sourceRow, int count, int destRow, int queueSize);

    /** @brief Убрать строки из модели (сервер уже изменён командой delete).
     *  @param rows Номера строк в произвольном порядке, возможны повторы.
     *
     *  Удалять нужно одним блоком операций удаления — иначе вьюха получит набор
     *  несовместимых извещений о строках, которые уже сдвинулись.
     */
    void removeRowsAt(const QList<int> &rows);

    /** @brief Обновить теги строк, не трогая состав и порядок очереди.
     *  @param rows Трэки из `plchanges` со своими `pos`; строки вне диапазона игнорируются.
     *
     *  Нужен потому, что `update` в фоне меняет теги, не двигая ни одной строки:
     *  reset модели на такое сбросил бы выделение и прокрутку очереди.
     */
    void applyTagsRefreshed(const QList<SongEntry> &rows);

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
    /// Emit после того, как очередь заменена целиком (setSongs()).
    void loaded();

    /** @brief Emitted when the resolved current song changes.
     *  @param songId  Id of the new current song, -1 when nothing plays.
     */
    void currentSongChanged(int songId);

private:
    /// Emit currentSongChanged() if the resolved entry differs from last time.
    void notifyCurrentSong();

    /// Проставить pos по номеру строки (после перемещения или удаления).
    void renumberPositions();

    QList<SongEntry> songs_;
    int curId_ = -1;        ///< `songid` from MPD status
    int curPos_ = -1;       ///< `song` (queue position) from MPD status
    int notifiedId_ = -2;   ///< last id passed to currentSongChanged (-2: none yet)
    /// True once MPD has named a track; before that "no current song" is not a
    /// change worth announcing, it is simply the initial state.
    bool toldCurrent_ = false;
};

