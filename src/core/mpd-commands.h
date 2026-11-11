#pragma once

/**
 * @file mpd-commands.h
 * @brief Convenience wrapper around MPD player commands.
 *
 * Each public slot maps directly to one or two libmpdclient
 * mpd_run_*() calls.  The raw libmpdclient handle is accessed
 * through Connection::raw() via the MPD macro.
 */

#include <optional>

#include <QObject>
#include <QVector>
#include <mpd/tag.h>
#include "connection.h"
#include "core/playlist-model.h"   // SongEntry — тип возврата songForUri()

/// Точное ограничение «тег == значение» для findadd (добавление группы/альбома).
struct MpdTagConstraint {
    enum mpd_tag_type tag;
    QString value;
};

/// Stateless set of player-command slots that operate on a Connection.
class MpdCommands : public QObject {
    Q_OBJECT
public:
    /** @brief Construct with an existing (possibly disconnected) connection.
     *  @param conn    Owning Connection — must outlive this object.
     *  @param parent  Qt parent.
     */
    explicit MpdCommands(Connection *conn, QObject *parent = nullptr);

public slots:
    // -- transport ----------------------------------------------------------
    virtual void play();
    virtual void pause();
    virtual void togglePause();
    virtual void stop();
    virtual void next();
    virtual void previous();

    // -- seeking ------------------------------------------------------------
    /** @brief Seek to an absolute position.
     *  @param seconds  Target position.
     */
    virtual void seek(int seconds);

    /** @brief Seek relative to current position.
     *  @param delta  Signed offset in seconds.
     */
    virtual void seekRelative(int delta);

    // -- volume -------------------------------------------------------------
    /** @brief Set volume (0–100). */
    virtual void setVolume(int vol);
    virtual void volumeUp(int step = 5);
    virtual void volumeDown(int step = 5);

    // -- queue manipulation -------------------------------------------------
    /** @brief Add a URI (song or directory) to the queue. */
    virtual void addUri(const QString &uri);

    /** @brief Воспроизвести трэк, добавив его в очередь, если там его нет.
     *  @param uri  файл из базы (не каталог).
     *
     *  Семантика play_path() оригинального gmpc (`Tools/mpdinteraction.c:477`):
     *  трэк уже в очереди → `playid` существующего идентификатора, иначе `addid` +
     *  `playid`. Очередь не очищается — двойной клик в браузере не имеет права
     *  выбрасывать то, что пользователь собрал раньше.
     */
    virtual void activateUri(const QString &uri);

    /** @brief Play a specific queue position. */
    virtual void playPos(int pos);

    /** @brief Clear the entire queue (current playlist). */
    virtual void clearQueue();

    /** @brief Удалить из очереди треки по их идентификаторам.
     *  @param ids  songid'ы; отрицательные пропускаются.
     *
     *  Идентификаторы, а не позиции: очередь могла сдвинуться, пока пользователь
     *  выбирал строки (другой клиент, consume-режим).
     */
    virtual void removeSongIds(const QList<int> &ids);

    /** @brief Переместить блок строк очереди на новую позицию.
     *  @param fromPos  Первая позиция блока.
     *  @param count    Длина блока.
     *  @param toPos    Позиция первой строки блока ПОСЛЕ перемещения — именно так
     *                  MPD трактует аргумент TO: очередь в момент перестановки уже
     *                  без переносимых строк.
     */
    virtual void moveQueueBlock(int fromPos, int count, int toPos);

    /** @brief Сохранить текущую очередь как список воспроизведения.
     *  @param name  Имя в каталоге плейлистов MPD; существующее перезаписывается.
     */
    virtual void saveQueueAs(const QString &name);

    // -- списки воспроизведения (сохранённые) --------------------------------
    /** @brief Добавить в очередь все трэки базы под ограничениями (`findadd`).
     *  @param constraints  Пары «тег == значение»; точное совпадение.
     *
     *  Пустой список ограничений — намеренный no-op: `findadd` без фильтра это
     *  «всю базу в очередь», а не «ничего».
     *
     *  Дубликаты MPD не пропускает (замерено на MPD 0.25): строка, которая уже стоит
     *  в очереди, будет добавлена второй раз. Это поведение оригинального gmpc — двойной
     *  клик по группе означает «собрать группу в очередь», а не «добавить недостающее».
     */
    virtual void findAddSongs(const QVector<MpdTagConstraint> &constraints);

    /** @brief Дописать сохранённый список воспроизведения в очередь (`load`).
     *
     *  Именно дописывает, а не заменяет (замерено на MPD 0.25: очередь из 7 строк после
     *  `load` списка той же длины стала 14). Очередь пользователя двойной клик выбрасывать
     *  не имеет права.
     */
    virtual void loadStoredPlaylist(const QString &name);

    /** @brief Удалить позицию из сохранённого списка воспроизведения.
     *  @param name      Имя списка, как его отдаёт `listplaylists`.
     *  @param position  Номер строки в этом списке (не в очереди).
     *
     *  Очередь не меняется: `playlistdelete` правит файл списка на сервере (замерено).
     */
    virtual void deletePlaylistPosition(const QString &name, int position);

    /** @brief Дописать URI в сохранённый список (`playlistadd`).
     *  @param name  Имя списка; файла нет — он создаётся (замерено на MPD 0.25).
     *  @param uri   Трэк или каталог: каталог сервер разворачивает сам.
     *
     *  Дубликаты разрешены (замер 16): две одинаковые строки в списке — норма, а не
     *  ошибка. Несуществующий URI → ACK [50@0] No such directory (замер 17).
     */
    virtual void appendToStoredPlaylist(const QString &name, const QString &uri);

    /** @brief Создать пустой сохранённый список по имени.
     *
     *  Одной команды «создать» у MPD нет: `playlistclear` требует существующий файл
     *  (ACK [50@0] {playlistclear} No such playlist, замер 15). Поэтому два шага —
     *  `save` текущей очереди под именем (создаёт или перезаписывает) и `playlistclear`
     *  (обнуляет); очередь при этом не трогается.
     */
    virtual void createEmptyStoredPlaylist(const QString &name);

    /** @brief Переместить блок позиций сохранённого списка (`playlistmove`).
     *  @param name      Имя списка.
     *  @param fromPos   Первая строка блока в файле списка.
     *  @param count     Длина блока.
     *  @param toPos     Позиция первой строки ПОСЛЕ перемещения — координаты без
     *                   вынутого блока, как у `move_range` очереди (замер 16:
     *                   допустимо `to ≤ len−count`, иначе ACK 2 Bad range).
     */
    virtual void movePlaylistBlock(const QString &name, int fromPos, int count, int toPos);

    /** @brief Удалить подряд идущий блок позиций сохранённого списка.
     *  @param name      Имя списка.
     *  @param firstRow  Первая удаляемая строка (включительно).
     *  @param lastRow   Последняя удаляемая строка (включительно) — переводится в
     *                   полуинтервал, как требует диапазонная форма `playlistdelete`.
     *
     *  Один вызов вместо цикла: сырая форма «start end» серверу не нравится
     *  («wrong number of arguments», замер 17), а библиотечная работает и экономит
     *  round-trip на каждой строке. Несмежное выделение всё равно требует по вызову
     *  на блок — см. PlaylistEditor::removalBlocks().
     */
    virtual void removePlaylistPositions(const QString &name, int firstRow, int lastRow);

    // -- playback modes -----------------------------------------------------
    virtual void setRepeat(bool on);
    virtual void setRandom(bool on);
    virtual void setSingle(bool on);
    virtual void setConsume(bool on);

    virtual void toggleRepeat();
    virtual void toggleRandom();
    virtual void toggleSingle();
    virtual void toggleConsume();

    /// Toggle mute: remembers the last non-zero volume.
    virtual void muteToggle();

    /** @brief Теги трэка по точному URI из базы — для окна «Song Info».
     *
     *  Намерение строки браузера несёт только путь, а показать надо artist/title/album:
     *  их знает сервер. Поиск идёт **по базе**, не по очереди: трэк выбирают и там, где он
     *  в очередь вовсе не поставлен.
     *
     *  @returns найденный трэк либо nullopt (нет соединения, пустой URI или файла нет).
     */
    [[nodiscard]] std::optional<SongEntry> songForUri(const QString &uri) const;

private:
    /** @brief Идентификатор трэка в очереди по точному совпадению URI.
     *  @returns songid или -1, если трэка в очереди нет (и вне соединения).
     */
    int queueSongIdForUri(const QString &uri) const;

    Connection *conn_;
};

