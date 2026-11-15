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
#include <QStringList>
#include <QVector>
#include <mpd/tag.h>
#include "connection.h"
#include "core/playlist-model.h"   // SongEntry — тип возврата songForUri()

/// Точное ограничение «тег == значение» для findadd (добавление группы/альбома).
struct MpdTagConstraint {
    enum mpd_tag_type tag;
    QString value;
};

/** @brief Снимок ответа сервера на команду `stats` — содержимое окна «Server Information».
 *
 *  Всё, кроме version и urlHandlers, приходит одним ответом: сервер сам считает аптайм
 *  и суммарную длительность базы. Числа держим в quint64 — секунды аптайма демона,
 *  работающего месяцами, перерастают int.
 */
struct ServerStats {
    QString version;           ///< версия протокола сервера («0.25.6»); пусто, если не сообщена
    quint64 uptimeSec = 0;     ///< сколько работает сам демон
    quint64 playtimeSec = 0;   ///< сколько он играл
    quint64 dbPlaytimeSec = 0; ///< суммарная длительность всей базы
    int artists = 0;
    int albums = 0;
    int songs = 0;
    QStringList urlHandlers;   ///< схемы («http://»…); пусто — сервер их не объявил
};

/** @brief Выходное аудиоустройство сервера (команда `outputs`) — строка окна настроек. */
struct MpdOutput {
    int id = -1;        ///< идентификатор устройства: имена дублируются, он уникален
    QString name;
    bool enabled = false;
};

/** @brief Время и число песен с одним значением тега (`count`). */
struct TagPlaytime {
    QString value;             ///< значение, как его отдал сервер
    quint64 playTimeSec = 0;
    int songs = 0;
};

/** @brief Секунды в компактную подпись («2 m 5 s», «1 d 1 h»).
 *
 *  Оригинал печатал локализованную прозу (`format_time_real`), у нас фиксированный
 *  набор единиц: разбор не должен зависеть от языка. Нулевые компоненты опускаются,
 *  полностью нулевое время — «0 s».
 */
[[nodiscard]] QString formatSpan(quint64 seconds);

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

    /** @brief Отдать серверу произвольный адрес (`load <адрес>`) — пункт «Add URL».
     *
     *  Оригинал (`Tools/url-fetcher.c:555`, `url_start_real()`) на сервере ≥ 0.16
     *  отдавал адрес самому MPD и только при его отказе качал клиентом своим
     *  фетчером; клентского фетчера у нас нет, поэтому отказ виден в логе
     *  (`consumeError`) — врать «добавлено» на него не будем.
     *
     *  @param url адрес схемы (`http://…`), имя сохранённого списка или путь
     */
    virtual void addUrl(const QString &url);

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

    // -- статистика сервера (окно «Server Information») ----------------------
    /** @brief Снимок `stats`: аптайм, время игры и размеры базы.
     *  @returns поля ответа либо nullopt (нет соединения или команда отвергнута).
     */
    virtual std::optional<ServerStats> serverStats() const;

    /** @brief Схемы, которые сервер умеет открывать как URL (`urlhandlers`).
     *
     *  Пусто и вне соединения, и когда сервер таких схем не объявил: окну различать
     *  эти случаи незачем.
     */
    virtual QStringList urlSchemes() const;

    /** @brief Значения одного тега по всей базе (`list <tag>`) — строки таблицы
     *  «Tag statistics».
     *
     *  Пусто и вне соединения, и когда такого тега в базе нет.
     */
    virtual QStringList tagValues(enum mpd_tag_type tag) const;

    /** @brief Теги, включённые на сервере (`tagtypes`) — варианты списка в окне.
     *
     *  Спрашиваем сервер, а не перебираем весь `mpd_tag_type`: тег может быть
     *  отключён (disabled tag types), и обход по нему вернул бы пустоту на каждой
     *  строке — таблица из одних нулей.
     */
    virtual QVector<enum mpd_tag_type> supportedTags() const;

    /** @brief Время песен с данным значением тега (`count` + ограничение).
     *
     *  Отдельная команда на значение: агрегата «сразу по всем значениям» у MPD нет,
     *  поэтому таблицу окно собирает построчно, чанками — иначе база в тысячи
     *  исполнителей заморозила бы GUI-поток.
     */
    virtual std::optional<TagPlaytime> tagPlaytime(enum mpd_tag_type tag,
                                                  const QString &value) const;

    // -- серверные настройки: fading, выходы, ReplayGain ---------------------
    /** @brief Выходные устройства сервера (`outputs`); пусто вне соединения. */
    [[nodiscard]] virtual QVector<MpdOutput> outputs() const;

    /** @brief Включить или выключить выход.
     *
     *  По идентификатору, а не по имени: имена дублируются (несколько «null output»
     *  в конфигурации — норма), уникален только id. Состояние сервер меняет сам,
     *  локальной копии у нас нет.
     */
    virtual void setOutputEnabled(int id, bool on);

    /** @brief Текущий crossfade из статуса; -1 вне соединения (0 — это «выключен»).
     *
     *  Возврат int, а не unsigned: различать «ноль секунд» и «нет ответа» обязательно.
     */
    [[nodiscard]] virtual int crossfadeSeconds();

    /// Установить crossfade в секундах; 0 выключает (команда `crossfade`).
    virtual void setCrossfade(int seconds);

    /** @brief Режим ReplayGain сервера (`replay_gain_status`) как значение
     *  enum mpd_replay_gain_mode: Off/Track/Album/Auto — индексы выпадающего списка.
     *  -1 вне соединения либо при неизвестном режиме. */
    [[nodiscard]] virtual int replayGainMode();

    /// Установить режим ReplayGain (значение enum mpd_replay_gain_mode).
    virtual void setReplayGainMode(int mode);

private:
    /** @brief Идентификатор трэка в очереди по точному совпадению URI.
     *  @returns songid или -1, если трэка в очереди нет (и вне соединения).
     */
    int queueSongIdForUri(const QString &uri) const;

    Connection *conn_;
};

