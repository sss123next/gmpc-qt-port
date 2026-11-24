/**
 * @file current-playlist.cpp
 * @brief Current playlist view implementation.
 *
 * Очередь на экране — зеркало MPD: любое изменение сначала уходит серверу, и лишь
 * потом меняется локальная копия. Иначе отказ команды оставил бы список в порядке,
 * которого на сервере нет, а следующий `plchanges` (7.2) привёл бы его в соответствие
 * уже помимо воли пользователя.
 */

#include "current-playlist.h"
#include "browser-intent.h"
#include "core/config.h"
#include "core/connection.h"
#include "core/mpd-commands.h"
#include "core/playlist-model.h"
#include "core/queue-sync.h"
#include "gui/column-chooser-dialog.h"
#include "tools/log.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDropEvent>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QShortcut>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

/** @brief Таблица очереди, открывающая позицию индикатора дропа.
 *  Индикатор Qt рисует сам, а спросить, где именно он нарисован, может только
 *  потомок: и dropIndicatorPosition(), и сам enum DropIndicatorPosition объявлены
 *  protected, поэтому наружу отдаём уже готовый ответ, а не значение енума.
 *
 * QTableView вместо QTreeView: в очереди нет иерархии, а QTreeView по умолчанию
 * блокирует перемещение первой колонки. */
class QueueTableView : public QTableView {
public:
    using QTableView::QTableView;

    /// Expose horizontalHeader publicly (QTableView::header() is protected).
    QHeaderView *header() const { return horizontalHeader(); }

    /// Индикатор под строкой — значит, блок встанет после неё (иначе — перед).
    bool dropIndicatorBelowItem() const
    {
        return dropIndicatorPosition() == BelowItem;
    }
};

/// Смежный ли блок строк выделен: `move_range` переносит ровно один такой блок.
bool contiguousBlock(const QList<int> &rows, int *first, int *count)
{
    if (rows.isEmpty())
        return false;
    for (int i = 1; i < rows.size(); ++i) {
        if (rows[i] != rows[i - 1] + 1)
            return false;
    }
    *first = rows.first();
    *count = rows.size();
    return true;
}

} // namespace

CurrentPlaylist::CurrentPlaylist(Connection *conn, MpdCommands *cmds,
                                 PlaylistModel *model, QWidget *parent)
    : BrowserWidget(parent), conn_(conn), cmds_(cmds), model_(model)
    , proxy_(new QSortFilterProxyModel(this))
    , filterEdit_(new QLineEdit(this))
{
    proxy_->setSourceModel(model_);
    proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy_->setFilterKeyColumn(-1); // search all columns
    // Длительность сортируется численно: по DisplayRole «10:00» встал бы раньше «9:00».
    proxy_->setSortRole(PlaylistModel::SortRole);

    setupUi();

    // Filter activated by Config — see setSearchAsType() called from MainWindow.
    connect(filterEdit_, &QLineEdit::returnPressed, this, [this] {
        onFilterChanged(filterEdit_->text());
    });
}

void CurrentPlaylist::setupUi()
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    filterEdit_->setPlaceholderText(tr("Search playlist…"));
    filterEdit_->setClearButtonEnabled(true);
    lay->addWidget(filterEdit_);

    view_ = new QueueTableView(this);
    view_->setModel(proxy_);
    view_->setAlternatingRowColors(true);
    view_->setShowGrid(false);
    view_->setSortingEnabled(true);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setSelectionMode(QAbstractItemView::ExtendedSelection);

    auto *header = view_->horizontalHeader();
    header->setStretchLastSection(false);
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    header->setSectionsMovable(true);

    // Сохранённый порядок и видимость колонок — поверх исходной раскладки.
    applyColumnConfig();

    // Сохранять ширину колонок при ручном ресайзе (но не во время applyColumnConfig).
    connect(header, &QHeaderView::sectionResized, this,
            [this](int logicalIndex, int, int width) {
        if (applyingColumns_)
            return;
        const QString key = playlistColumnKey(logicalIndex);
        if (!key.isEmpty())
            Config::instance().setPlaylistColumnWidth(key, width);
    });

    // ————————————————————————————————————————————————————————————————————
    //  Контекстное меню заголовка: чекбоксы видимости колонок
    // ————————————————————————————————————————————————————————————————————
    connect(header, &QWidget::customContextMenuRequested,
            this, &CurrentPlaylist::showHeaderMenu);

    // Сохранять порядок колонок при перетаскивании заголовков мышью.
    connect(header, &QHeaderView::sectionMoved,
            this, &CurrentPlaylist::saveColumnOrder);

    // Перестановка перетаскиванием: Qt собирает mimeData модели и рисует индикатор,
    // событие дропа разбираем мы (см. eventFilter).
    view_->setDragEnabled(true);
    view_->setAcceptDrops(true);
    view_->setDropIndicatorShown(true);
    view_->setDragDropMode(QAbstractItemView::InternalMove);

    lay->addWidget(view_, 1);

    // Двойной клик и Enter — как row-activated в оригинальном gmpc.
    connect(view_, &QTableView::activated, this, [this](const QModelIndex &index) {
        playRow(sourceRowOf(index));
    });

    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view_, &QTableView::customContextMenuRequested,
            this, &CurrentPlaylist::showContextMenu);

    // Delete/Backspace удаляют выделенное, но только когда фокус внутри дерева:
    // на весь стек браузеров эти клавиши перехватывать нельзя.
    for (const QKeySequence &seq : {QKeySequence::Delete, QKeySequence::Backspace}) {
        auto *shortcut = new QShortcut(seq, view_);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, [this] {
            removeRows(selectedSourceRows());
        });
    }

    view_->viewport()->installEventFilter(this);
}

void CurrentPlaylist::showHeaderMenu(const QPoint &pos)
{
    auto &cfg = Config::instance();
    QHeaderView *header = view_->horizontalHeader();

    QMenu menu(this);

    for (int col = 0; col < PlaylistModel::ColumnCount; ++col) {
        const QString title = model_->headerData(col, Qt::Horizontal).toString();
        const QString key   = playlistColumnKey(col);
        const bool visible  = cfg.playlistColumnVisible(key);

        QAction *act = menu.addAction(title);
        act->setCheckable(true);
        act->setChecked(!header->isSectionHidden(col));
        act->setData(key);

        connect(act, &QAction::toggled, this, [this, &cfg, col, key](bool on) {
            QHeaderView *h = view_->horizontalHeader();
            h->setSectionHidden(col, !on);
            cfg.setPlaylistColumnVisible(key, on);
        });
    }

    menu.exec(header->mapToGlobal(pos));
}

void CurrentPlaylist::saveColumnOrder()
{
    if (applyingColumns_)
        return;

    auto &cfg = Config::instance();
    QHeaderView *header = view_->horizontalHeader();

    // Read visual order: iterate logical columns, sort by visual index.
    QVector<QPair<int, int>> byVisual; // (visual, logical)
    for (int col = 0; col < PlaylistModel::ColumnCount; ++col)
        byVisual.append({header->visualIndex(col), col});
    std::sort(byVisual.begin(), byVisual.end());

    QStringList order;
    for (const auto &pair : byVisual)
        order.append(playlistColumnKey(pair.second));

    cfg.setPlaylistColumnsOrder(order);
}

void CurrentPlaylist::applyColumnConfig()
{
    auto &cfg = Config::instance();
    QHeaderView *header = view_->horizontalHeader();

    for (int column = 0; column < PlaylistModel::ColumnCount; ++column) {
        const QString key = playlistColumnKey(column);
        const bool saved   = cfg.hasPlaylistColumnSetting(key);
        const bool isCore  = column <= PlaylistModel::TimeColumn
                             || column == PlaylistModel::PosColumn;

        // Core columns visible by default; extra columns hidden until
        // the user explicitly enables them via the header menu.
        const bool visible = saved
            ? cfg.playlistColumnVisible(key)
            : isCore;
        header->setSectionHidden(column, !visible);
    }

    // Resize modes.
    // TitleColumn — Stretch (заполняет ширину). Остальные — Interactive,
    // свободно перемещаются. QTableView не блокирует первую колонку.
    header->setStretchLastSection(false);
    for (int c = 0; c < PlaylistModel::ColumnCount; ++c) {
        if (c == PlaylistModel::TitleColumn)
            header->setSectionResizeMode(c, QHeaderView::Stretch);
        else
            header->setSectionResizeMode(c, QHeaderView::Interactive);
    }

    // ── Ширины колонок ──────────────────────────────────────────────
    // Восстановить из Config; если не сохранены — дефолтные.
    // Guard: sectionResized в setupUi() сохраняет ширину, но во время
    // программной установки сохранять не надо.
    const bool wasApplying = applyingColumns_;
    applyingColumns_ = true;
    header->resizeSection(PlaylistModel::ArtistColumn,
        cfg.playlistColumnWidth(QLatin1String("artist"), 180));
    header->resizeSection(PlaylistModel::AlbumColumn,
        cfg.playlistColumnWidth(QLatin1String("album"), 180));
    header->resizeSection(PlaylistModel::PosColumn,
        cfg.playlistColumnWidth(QLatin1String("pos"), 36));
    // Time: ResizeToContents при первом появлении, но пользователь может
    // изменить ширину вручную. Сохраняем как "time".
    if (cfg.playlistColumnWidth(QLatin1String("time")) < 0)
        header->resizeSection(PlaylistModel::TimeColumn,
                              header->defaultSectionSize());
    applyingColumns_ = wasApplying;

    // Порядок: каждый сохранённый ключ встаёт на следующее место подряд.
    // Если порядка нет — ставим колонку # первой, затем Title/Artist/Album/Time
    // и скрытые extra-колонки.
    // Guard the programmatic moves: sectionMoved signal must not trigger
    // saveColumnOrder during initial layout.
    applyingColumns_ = true;

    QStringList order = cfg.playlistColumnsOrder();
    if (order.isEmpty()) {
        order = {
            QStringLiteral("pos"),
            QStringLiteral("title"),
            QStringLiteral("artist"),
            QStringLiteral("album"),
            QStringLiteral("time"),
        };
        for (int col = PlaylistModel::TrackColumn; col < PlaylistModel::ColumnCount; ++col)
            order.append(playlistColumnKey(col));
    }

    int target = 0;
    for (const QString &key : order) {
        const int column = playlistColumnForKey(key);
        if (column < 0)
            continue;   // ключ из другой версии — нашего столбца за ним нет
        header->moveSection(header->visualIndex(column), target++);
    }
    applyingColumns_ = false;
}

void CurrentPlaylist::editColumns()
{
    QHeaderView *header = view_->horizontalHeader();

    // Текущий порядок берём с экрана, а не из Config: пользователь мог тасовать
    // столбцы мышью за заголовок, и окно обязано показать то, что он видит.
    QVector<QPair<int, int>> byVisual;   // (визуальная позиция, колонка)
    for (int column = 0; column < PlaylistModel::ColumnCount; ++column)
        byVisual.append({header->visualIndex(column), column});
    std::sort(byVisual.begin(), byVisual.end());

    QVector<ColumnChoice> choices;
    for (const auto &pair : byVisual) {
        const int column = pair.second;
        ColumnChoice choice;
        choice.key     = playlistColumnKey(column);
        choice.title   = model_->headerData(column, Qt::Horizontal).toString();
        choice.visible = !header->isSectionHidden(column);
        choices.append(choice);
    }

    ColumnChooserDialog dlg(this);
    dlg.setColumns(choices);
    if (dlg.exec() != QDialog::Accepted)
        return;

    applyColumnChoices(dlg.columns());
}

void CurrentPlaylist::applyColumnChoices(const QVector<ColumnChoice> &choices)
{
    auto &cfg = Config::instance();
    QStringList order;
    for (const ColumnChoice &choice : choices) {
        cfg.setPlaylistColumnVisible(choice.key, choice.visible);
        order.append(choice.key);   // порядок окна — он и есть новый
    }
    cfg.setPlaylistColumnsOrder(order);

    applyColumnConfig();
}

// ── Разбор индексов вида ─────────────────────────────────────────────────────

int CurrentPlaylist::sourceRowOf(const QModelIndex &index) const
{
    if (!index.isValid())
        return -1;
    // Вид показывает прокси: сортировка и фильтр смещают строки относительно очереди.
    const QModelIndex source = proxy_->mapToSource(index);
    return source.isValid() ? source.row() : -1;
}

QList<int> CurrentPlaylist::selectedSourceRows() const
{
    QList<int> rows;
    auto *selection = view_->selectionModel();
    if (!selection)
        return rows;

    for (const QModelIndex &index : selection->selectedRows(PlaylistModel::TitleColumn)) {
        const int row = sourceRowOf(index);
        if (row >= 0 && !rows.contains(row))
            rows.append(row);
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

// ── Действия над очередью ────────────────────────────────────────────────────

void CurrentPlaylist::playRow(int sourceRow)
{
    if (sourceRow < 0 || !conn_->isConnected())
        return;
    const SongEntry *song = model_->songAt(sourceRow);
    if (song && song->pos >= 0)
        cmds_->playPos(song->pos);
}

void CurrentPlaylist::removeRows(const QList<int> &sourceRows)
{
    if (sourceRows.isEmpty() || !conn_->isConnected())
        return;

    // Удаляем по идентификаторам: пока пользователь выбирал строки, очередь могла
    // сдвинуться (другой клиент, consume-режим).
    QList<int> ids;
    for (int row : sourceRows) {
        const SongEntry *song = model_->songAt(row);
        if (song && song->id >= 0)
            ids.append(song->id);
    }

    cmds_->removeSongIds(ids);
    model_->removeRowsAt(sourceRows);
}

void CurrentPlaylist::moveBlock(int sourceRow, int count, int destRow)
{
    if (!conn_->isConnected())
        return;

    const int toPos = PlaylistModel::targetPosForMove(sourceRow, count, destRow,
                                                      model_->rowCount({}));
    if (toPos < 0)
        return;

    cmds_->moveQueueBlock(sourceRow, count, toPos);
    // Отказ команды виден в логе; локальная копия разъедется с очередью ровно до
    // ближайшего обновления по idle (7.2), где очередь и синхронизируется заново.
    model_->moveRowsWithin(sourceRow, count, destRow);
}

void CurrentPlaylist::moveBlockFromIds(const QList<int> &songIds, int destRow)
{
    // pos == номер строки в полном списке очереди, так что идентификатор переводится
    // в строку одним поиском.
    QList<int> rows;
    for (int id : songIds) {
        const int row = model_->posForId(id);
        if (row >= 0 && !rows.contains(row))
            rows.append(row);
    }
    std::sort(rows.begin(), rows.end());

    int first = 0, count = 0;
    if (!contiguousBlock(rows, &first, &count)) {
        LOG_INFO("очередь: непоследовательное выделение не перемещается ({} строк)",
                 rows.size());
        return;
    }
    moveBlock(first, count, destRow);
}

void CurrentPlaylist::moveSelectionBy(int delta)
{
    int first = 0, count = 0;
    if (!contiguousBlock(selectedSourceRows(), &first, &count))
        return;

    // «Перед строкой» в координатах до перемещения: вверх — на строку раньше,
    // вниз — за строку, следующую за блоком.
    const int destRow = delta < 0 ? first - 1 : first + count + 1;
    moveBlock(first, count, destRow);
}

// ── События ─────────────────────────────────────────────────────────────────

bool CurrentPlaylist::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Drop && watched == view_->viewport()) {
        auto *drop = static_cast<QDropEvent *>(event);
        const QList<int> ids = PlaylistModel::songIdsFromMimeData(drop->mimeData());

        if (!ids.isEmpty() && drop->proposedAction() == Qt::MoveAction) {
            int destRow = -1;   // за последнюю строку — в конец очереди
            const QModelIndex target = view_->indexAt(drop->position().toPoint());
            if (target.isValid()) {
                const int row = sourceRowOf(target);
                // «На строку» означает «перед ней» — так же Qt вставляет элемент в списке.
                // Наследник нужен лишь затем, чтобы прочитать позицию индикатора;
                // дерево создаётся здесь же, в setupUi(), поэтому приведение безопасно.
                destRow = static_cast<QueueTableView *>(view_)->dropIndicatorBelowItem()
                    ? row + 1 : row;
            }

            moveBlockFromIds(ids, destRow);
            drop->acceptProposedAction();
            // Базовая обработка ушла бы в model()->dropMimeData, которой у нас нет:
            // перестановка очереди — не перемещение данных внутри модели.
            return true;
        }
    }

    return BrowserWidget::eventFilter(watched, event);
}

void CurrentPlaylist::showContextMenu(const QPoint &pos)
{
    const QList<int> rows = selectedSourceRows();
    const bool connected = conn_->isConnected();

    int first = 0, count = 0;
    const bool oneBlock = contiguousBlock(rows, &first, &count);

    QMenu menu(this);
    QAction *playAct = menu.addAction(tr("Play"));
    playAct->setEnabled(!rows.isEmpty() && connected);
    QAction *removeAct = menu.addAction(tr("Remove from queue"));
    removeAct->setEnabled(!rows.isEmpty() && connected);
    menu.addSeparator();
    QAction *upAct = menu.addAction(tr("Move up"));
    QAction *downAct = menu.addAction(tr("Move down"));
    upAct->setEnabled(oneBlock && connected);
    downAct->setEnabled(oneBlock && connected);

    // Путь берётся из URI очереди: в очереди трэк живёт тем же путём, что и в базе.
    QVector<BrowserIntent> intents;
    for (int row : rows) {
        const QString uri = model_->index(row, 0).data(PlaylistModel::UriRole).toString();
        if (uri.isEmpty())
            continue;

        BrowserIntent intent;
        intent.kind = BrowserIntent::Kind::Song;
        intent.uri = uri;
        intents.append(intent);
    }
    const QString musicDir = Config::instance().musicDirectory();
    menu.addSeparator();
    const BrowserPathMenuItems paths = addPathMenuItems(menu, intents, musicDir);

    QAction *infoAct = nullptr;
    if (!intents.isEmpty())
        infoAct = menu.addAction(tr("Info"));

    menu.addSeparator();
    const bool hasQueue = connected && model_->rowCount({}) > 0;
    QAction *clearAct = menu.addAction(tr("Clear queue"));
    clearAct->setEnabled(hasQueue);
    QAction *saveAct = menu.addAction(tr("Save queue as…"));
    saveAct->setEnabled(hasQueue);

    // Колонки — свойство вида, а не очереди: пункт живёт и без соединения.
    menu.addSeparator();
    QAction *colsAct = menu.addAction(tr("Edit columns…"));

    QAction *chosen = menu.exec(view_->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == playAct) {
        playRow(rows.isEmpty() ? -1 : rows.first());
    } else if (chosen == removeAct) {
        removeRows(rows);
    } else if (chosen == upAct) {
        moveSelectionBy(-1);
    } else if (chosen == downAct) {
        moveSelectionBy(1);
    } else if (paths.openFolder && chosen == paths.openFolder) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(paths.folder));
    } else if (paths.copyPath && chosen == paths.copyPath) {
        QApplication::clipboard()->setText(copyTextFor(intents, musicDir));
    } else if (infoAct && chosen == infoAct) {
        // Первая выделенная песня: окно одно, показывать Info толпой бессмысленно.
        if (!intents.isEmpty())
            emit infoRequested(intents.first().uri);
    } else if (chosen == clearAct) {
        clearQueueWithConfirm();
    } else if (chosen == saveAct) {
        saveQueueAs();
    } else if (chosen == colsAct) {
        editColumns();
    }
}

void CurrentPlaylist::saveQueueAs()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save queue as"),
                                               tr("Playlist name:"),
                                               QLineEdit::Normal, {}, &ok);
    if (ok && !name.trimmed().isEmpty())
        cmds_->saveQueueAs(name.trimmed());
}

void CurrentPlaylist::clearQueueWithConfirm()
{
    const auto answer = QMessageBox::question(this, tr("Clear queue"),
        tr("Remove all %1 song(s) from the queue?").arg(model_->rowCount({})));
    if (answer != QMessageBox::Yes)
        return;

    cmds_->clearQueue();
    model_->clear();
}

void CurrentPlaylist::onFilterChanged(const QString &text)
{
    proxy_->setFilterFixedString(text);
}

void CurrentPlaylist::setSearchAsType(bool on)
{
    disconnect(textChangedConn_);
    if (on) {
        textChangedConn_ = connect(filterEdit_, &QLineEdit::textChanged,
                                   this, &CurrentPlaylist::onFilterChanged);
    }
}

// ── Low Memory Mode lifecycle ─────────────────────────────────────────────

void CurrentPlaylist::onActivate()
{
    if (!Config::instance().lowMemoryMode())
        return;
    // Load the queue from MPD when the tab becomes visible.
    if (queueSync_)
        queueSync_->reload();
}

void CurrentPlaylist::onDeactivate()
{
    if (!Config::instance().lowMemoryMode())
        return;

    // Keep only the current song in the model so other widgets (NowPlaying,
    // control bar, mini-player) can still look up current track info.
    const SongEntry *cur = model_->currentSong();
    if (cur)
        model_->setSongs({*cur});
    else
        model_->clear();

    // Free the QueueSync worker's internal queue copy too — it holds the full
    // list even after the model is cleared (~10 MB for 65k entries).
    if (queueSync_)
        queueSync_->reset();
}
