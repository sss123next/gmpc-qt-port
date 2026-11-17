#pragma once

/**
 * @file current-playlist.h
 * @brief Current (now-playing) playlist browser.
 *
 * Displays the MPD queue in a QTreeView bound to a PlaylistModel and offers the
 * queue manipulation the original gmpc had: play by double-click, remove, move,
 * clear, save as, and reordering by drag&drop.
 */

#include "browser-widget.h"
#include "gui/column-chooser-dialog.h"

#include <QVector>
#include <QTreeView>
#include <QLineEdit>
#include <QList>
#include <QSortFilterProxyModel>

class Connection;
class MpdCommands;
class PlaylistModel;
class QueueSync;

/// View of the current MPD queue (the "now playing" list).
class CurrentPlaylist : public BrowserWidget {
    Q_OBJECT
public:
    /** @brief Construct the browser over a shared queue model.
     *  @param conn   Connection used for the connected-check before commands.
     *  @param cmds   Commands applied to MPD before the local mirror changes.
     *  @param model  Queue model owned by MainWindow (shared with other widgets).
     */
    explicit CurrentPlaylist(Connection *conn, MpdCommands *cmds,
                             PlaylistModel *model, QWidget *parent = nullptr);

    QString title() const override { return tr("Current"); }
    QString iconName() const override { return QStringLiteral("media-playback-start"); }

    /// The embedded tree view (for context menu setup).
    QTreeView *treeView() const { return view_; }

    /** @brief Записать выбор колонок в Config и применить его к виду.
     *
     *  Отделено от окна выбора: `exec()` модален, а проверять хочется без него.
     */
    void applyColumnChoices(const QVector<ColumnChoice> &choices);

    /** @brief Сохранить очередь как список воспроизведения (`save`).
     *
     *  Имя спрашивается окном. Живёт здесь, а не в меню: тот же пункт нужен и
     *  контекстному меню очереди, и главному меню — два места одного действия.
     */
    void saveQueueAs();
    /// Очистить очередь с подтверждением (`clear`): команда необратима.
    void clearQueueWithConfirm();

    /** @brief Toggle live filtering (textChanged) vs Enter-only search. */
    void setSearchAsType(bool on);

    /** @brief Provide QueueSync for deferred queue loading in Low Memory Mode. */
    void setQueueSync(QueueSync *sync) { queueSync_ = sync; }

    void onActivate() override;
    void onDeactivate() override;

protected:
    /** @brief Разбирает событие дропа: Qt умеет рисовать индикатор и собирать
     *  mimeData, но команду перестановки очереди знает только этот виджет. */
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onFilterChanged(const QString &text);
    /** @brief Показать контекстное меню очереди. */
    void showContextMenu(const QPoint &pos);

private:
    void setupUi();
    /** @brief Привести вид к сохранённым порядку и видимости колонок.
     *
     *  Порядок задаётся ключами (`PlaylistModel::columnKey`), а не номерами: сам
     *  enum переживает добавление столбца, но настройка пользователя обязана
     *  остаться при своей колонке.
     */
    void applyColumnConfig();
    /// Открыть окно выбора колонок и записать его результат в Config.
    void editColumns();

    /// Строки исходной модели из выделения (отсортированы, без повторов).
    QList<int> selectedSourceRows() const;
    /// Позиция строки в исходной модели для индекса вида (видит прокси).
    int sourceRowOf(const QModelIndex &index) const;

    /// Начать воспроизведение трека из строки очереди.
    void playRow(int sourceRow);
    /// Убрать выделенные строки из очереди.
    void removeRows(const QList<int> &sourceRows);
    /** @brief Перенести блок строк, выполнив команду серверу.
     *  @param destRow Строка, ПЕРЕД которой встанет блок (-1 — в конец).
     */
    void moveBlock(int sourceRow, int count, int destRow);
    /** @brief Переместить блок по идентификаторам из перетаскивания.
     *  Непоследовательное выделение не переносится: одна команда `move_range`
     *  описывает ровно один смежный блок.
     */
    void moveBlockFromIds(const QList<int> &songIds, int destRow);
    /// Сдвиг выделенного блока на одну строку (контекстное меню).
    void moveSelectionBy(int delta);

    Connection *conn_;
    MpdCommands *cmds_;
    PlaylistModel *model_;
    QueueSync *queueSync_ = nullptr;
    QTreeView *view_;
    QLineEdit *filterEdit_;
    QSortFilterProxyModel *proxy_;
    QMetaObject::Connection textChangedConn_;  ///< tracking for setSearchAsType
};
