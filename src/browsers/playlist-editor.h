#pragma once

/**
 * @file playlist-editor.h
 * @brief MPD stored-playlist editor.
 *
 * Lists, creates, renames, deletes saved playlists, and shows contents.
 * Двойной клик по позиции списка воспроизводит её (play_path), Delete/Backspace и
 * пункт контекстного меню удаляют выделенные позиции из сохранённого списка
 * (`playlistdelete` диапазоном — блок подряд идущих строк одним вызовом).
 * Контекстные меню две: у панели имён — Load / Replace / New / Rename / Delete (как в
 * оригинальном редакторе), у позиции — Play / Add to Queue / Move up / Move down /
 * Copy path / Open folder / Remove from playlist.
 *
 * Правки идут прямо по файлу списка на сервере (`playlistadd`/`playlistmove`/
 * `playlistdelete`) — как в оригинальном редакторе: отдельного «сохранить» нет,
 * содержимое перечитывается сразу после команды, потому что idle-событие о правке
 * файла списка приходит не всегда.
 */

#include "browser-widget.h"
#include "browser-intent.h"

#include <QPoint>
#include <QStringList>

class Connection;
class QListWidget;
class QListWidgetItem;
class MpdCommands;
class QPushButton;

/// Manage MPD stored playlists.
class PlaylistEditor : public BrowserWidget {
    Q_OBJECT
public:
    /** @param conn  соединение с MPD (должно переживать редактор)
     *  @param cmds  команды очереди для активации позиции
     */
    explicit PlaylistEditor(Connection *conn, MpdCommands *cmds,
                            QWidget *parent = nullptr);
    ~PlaylistEditor() override = default;

    // BrowserWidget interface
    void onActivate() override { refreshPlaylists(); }
    void onDeactivate() override {}
    void onConnectionChanged(bool connected) override;
    QString title() const override { return tr("Playlists"); }
    QString iconName() const override { return QStringLiteral("media-playlist"); }

    /// Роль URI трэка в позиции списка (QListWidgetItem::data).
    static constexpr int UriRole = Qt::UserRole + 1;

    /** @brief Намерения выделенных позиций сохранённого списка: трэки с их URI.
     *  Позиция в файле и позиция в очереди — разные вещи, поэтому здесь именно файл.
     */
    [[nodiscard]] QVector<BrowserIntent> selectedIntents() const;

    /** @brief Номера выделенных позиций в порядке удаления — убывающие.
     *
     *  `playlistdelete` перенумеровывает список: после удаления нулевой строки бывшая
     *  вторая занимает первый номер, поэтому восходящий порядок снёс бы не те трэки
     *  (замер на MPD 0.25). Отдельный метод затем, чтобы этот порядок проверялся юнитом
     *  без сервера: по самому удалению он не виден.
     */
    [[nodiscard]] QList<int> removalRows() const;

    /** @brief Блоки подряд идущих выделенных строк для диапазонного удаления.
     *  @returns пары «первая; последняя» включительно, по убыванию первой строки.
     *
     *  Удаление диапазоном сдвигает номера всех следующих за ним позиций (замерено),
     *  поэтому блоки идут сверху вниз: тогда обработанные ранее номера не съезжают под
     *  руками. Несмежное выделение — это несколько пар, а не одна на весь диапазон:
     *  удалять «от первой до последней» значило бы снести и то, что между ними.
     */
    [[nodiscard]] QVector<QPair<int, int>> removalBlocks() const;

    /** @brief Создать пустой список с этим именем — без диалога.
     *
     *  Отделено от слота «New…» ради юнита: `QInputDialog::getText()` ждёт событий,
     *  а проверять надо саму команду и последующее выделение нового имени.
     */
    void newPlaylistNamed(const QString &name);

private slots:
    void onPlaylistSelected(int row);
    void onNewPlaylist();
    void onRenamePlaylist();
    void onDeletePlaylist();
    void onLoadPlaylist();
    /** @brief Двойной клик по позиции списка — воспроизвести этот трэк. */
    void onItemActivated(QListWidgetItem *item);
    /** @brief Удалить выделенные позиции из сохранённого списка (`playlistdelete`).
     *  Номера идут убывающими: `playlistdelete` переномеровывает список, и после
     *  удаления нулевой строки бывшая вторая стоит уже под первым номером (замер).
     */
    void removeCurrentPosition();
    /** @brief Сдвинуть выделенный блок позиций на delta строк (−1 вверх, +1 вниз).
     *
     *  Блок — только смежный: одна `playlistmove` описывает один непрерывный участок,
     *  а «сдвиг» несмежного выделения иначе разъехался бы с тем, что видит пользователь.
     */
    void moveSelection(int delta);

private:
    void refreshPlaylists();
    void refreshContents(const QString &playlistName);
    void updateButtonStates();
    /** @brief Имя выбранного сохранённого списка (пусто, если выбора нет). */
    QString currentPlaylistName() const;
    /** @brief Номера выделенных позиций по возрастанию; пусто — текущая строка.
     *
     *  Возвращаются именно номера строк виджета: команды правки файла списка работают
     *  с ними, а не с идентификаторами (у позиции сохранённого списка id нет).
     */
    QList<int> selectedContentsRows() const;
    /** @brief Меню позиции: play / add / copy path / open folder / remove. */
    void showContentsMenu(const QPoint &pos);
    /** @brief Меню панели имён: load / replace queue / new / rename / delete. */
    void showNamesMenu(const QPoint &pos);
    /** @brief Заменить очередь содержимым списка: `playlist_clear`, `load`, `play`. */
    void replaceQueueWithPlaylist();

    Connection *conn_;
    MpdCommands *cmds_;

    QListWidget *playlistList_;
    QListWidget *contentsList_;

    QPushButton *newBtn_;
    QPushButton *renameBtn_;
    QPushButton *deleteBtn_;
    QPushButton *loadBtn_;
};
