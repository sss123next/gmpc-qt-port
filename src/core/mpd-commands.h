#pragma once

/**
 * @file mpd-commands.h
 * @brief Convenience wrapper around MPD player commands.
 *
 * Each public slot maps directly to one or two libmpdclient
 * mpd_run_*() calls.  The raw libmpdclient handle is accessed
 * through Connection::raw() via the MPD macro.
 */

#include <QObject>
#include <QVector>
#include <mpd/tag.h>
#include "connection.h"

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
    void play();
    void pause();
    void togglePause();
    void stop();
    void next();
    void previous();

    // -- seeking ------------------------------------------------------------
    /** @brief Seek to an absolute position.
     *  @param seconds  Target position.
     */
    void seek(int seconds);

    /** @brief Seek relative to current position.
     *  @param delta  Signed offset in seconds.
     */
    void seekRelative(int delta);

    // -- volume -------------------------------------------------------------
    /** @brief Set volume (0–100). */
    void setVolume(int vol);
    void volumeUp(int step = 5);
    void volumeDown(int step = 5);

    // -- queue manipulation -------------------------------------------------
    /** @brief Add a URI (song or directory) to the queue. */
    void addUri(const QString &uri);

    /** @brief Воспроизвести трэк, добавив его в очередь, если там его нет.
     *  @param uri  файл из базы (не каталог).
     *
     *  Семантика play_path() оригинального gmpc (`Tools/mpdinteraction.c:477`):
     *  трэк уже в очереди → `playid` существующего идентификатора, иначе `addid` +
     *  `playid`. Очередь не очищается — двойной клик в браузере не имеет права
     *  выбрасывать то, что пользователь собрал раньше.
     */
    void activateUri(const QString &uri);

    /** @brief Play a specific queue position. */
    void playPos(int pos);

    /** @brief Clear the entire queue (current playlist). */
    void clearQueue();

    /** @brief Удалить из очереди треки по их идентификаторам.
     *  @param ids  songid'ы; отрицательные пропускаются.
     *
     *  Идентификаторы, а не позиции: очередь могла сдвинуться, пока пользователь
     *  выбирал строки (другой клиент, consume-режим).
     */
    void removeSongIds(const QList<int> &ids);

    /** @brief Переместить блок строк очереди на новую позицию.
     *  @param fromPos  Первая позиция блока.
     *  @param count    Длина блока.
     *  @param toPos    Позиция первой строки блока ПОСЛЕ перемещения — именно так
     *                  MPD трактует аргумент TO: очередь в момент перестановки уже
     *                  без переносимых строк.
     */
    void moveQueueBlock(int fromPos, int count, int toPos);

    /** @brief Сохранить текущую очередь как список воспроизведения.
     *  @param name  Имя в каталоге плейлистов MPD; существующее перезаписывается.
     */
    void saveQueueAs(const QString &name);

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
    void findAddSongs(const QVector<MpdTagConstraint> &constraints);

    /** @brief Дописать сохранённый список воспроизведения в очередь (`load`).
     *
     *  Именно дописывает, а не заменяет (замерено на MPD 0.25: очередь из 7 строк после
     *  `load` списка той же длины стала 14). Очередь пользователя двойной клик выбрасывать
     *  не имеет права.
     */
    void loadStoredPlaylist(const QString &name);

    /** @brief Удалить позицию из сохранённого списка воспроизведения.
     *  @param name      Имя списка, как его отдаёт `listplaylists`.
     *  @param position  Номер строки в этом списке (не в очереди).
     *
     *  Очередь не меняется: `playlistdelete` правит файл списка на сервере (замерено).
     */
    void deletePlaylistPosition(const QString &name, int position);

    // -- playback modes -----------------------------------------------------
    void setRepeat(bool on);
    void setRandom(bool on);
    void setSingle(bool on);
    void setConsume(bool on);

    void toggleRepeat();
    void toggleRandom();
    void toggleSingle();
    void toggleConsume();

    /// Toggle mute: remembers the last non-zero volume.
    void muteToggle();

private:
    /** @brief Идентификатор трэка в очереди по точному совпадению URI.
     *  @returns songid или -1, если трэка в очереди нет (и вне соединения).
     */
    int queueSongIdForUri(const QString &uri) const;

    Connection *conn_;
};

