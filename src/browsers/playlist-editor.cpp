/**
 * @file playlist-editor.cpp
 * @brief MPD stored-playlist editor.
 */

#include "playlist-editor.h"
#include "core/config.h"
#include "core/connection.h"
#include "core/mpd-commands.h"

#include <mpd/playlist.h>
#include <mpd/song.h>
#include <mpd/entity.h>

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

// ── Construction ────────────────────────────────────────────────────────────

PlaylistEditor::PlaylistEditor(Connection *conn, MpdCommands *cmds, QWidget *parent)
    : BrowserWidget(parent)
    , conn_(conn)
    , cmds_(cmds)
    , playlistList_(new QListWidget(this))
    , contentsList_(new QListWidget(this))
    , newBtn_(new QPushButton(tr("New"), this))
    , renameBtn_(new QPushButton(tr("Rename"), this))
    , deleteBtn_(new QPushButton(tr("Delete"), this))
    , loadBtn_(new QPushButton(tr("Load"), this))
{
    auto *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);

    // Имена панелей: обе — QListWidget, а порядок потомков зависит от раскладки, поэтому
    // отличать «позиции списка» от «списка имён» надо по имени, а не по индексу.
    playlistList_->setObjectName(QStringLiteral("playlistNames"));
    contentsList_->setObjectName(QStringLiteral("playlistContents"));

    // ── splitter: playlist list | contents list ──
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    auto *leftWrap = new QWidget(this);
    auto *leftLay = new QVBoxLayout(leftWrap);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->addWidget(new QLabel(tr("Playlists"), leftWrap));
    leftLay->addWidget(playlistList_);
    splitter->addWidget(leftWrap);

    auto *rightWrap = new QWidget(this);
    auto *rightLay = new QVBoxLayout(rightWrap);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->addWidget(new QLabel(tr("Contents"), rightWrap));
    rightLay->addWidget(contentsList_);
    splitter->addWidget(rightWrap);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    mainLay->addWidget(splitter);

    // ── button bar ──
    auto *btnLay = new QHBoxLayout;
    btnLay->addWidget(newBtn_);
    btnLay->addWidget(renameBtn_);
    btnLay->addWidget(deleteBtn_);
    btnLay->addStretch();
    btnLay->addWidget(loadBtn_);
    mainLay->addLayout(btnLay);

    // ── connections ──
    connect(playlistList_, &QListWidget::currentRowChanged,
            this, &PlaylistEditor::onPlaylistSelected);
    connect(newBtn_, &QPushButton::clicked,
            this, &PlaylistEditor::onNewPlaylist);
    connect(renameBtn_, &QPushButton::clicked,
            this, &PlaylistEditor::onRenamePlaylist);
    connect(deleteBtn_, &QPushButton::clicked,
            this, &PlaylistEditor::onDeletePlaylist);
    connect(loadBtn_, &QPushButton::clicked,
            this, &PlaylistEditor::onLoadPlaylist);

    // ── панель имён списков ──
    // Как в оригинальном редакторе: список имён живёт сам по себе, и меню у него про
    // сами списки (Load / Replace / New / Rename / Delete), а не про их содержимое.
    playlistList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(playlistList_, &QListWidget::customContextMenuRequested,
            this, [this](const QPoint &pos) { showNamesMenu(pos); });

    // ── позиции сохранённого списка ──
    // Двойной клик — как row-activated в оригинале: play_path(), а не «играть N-ю
    // строку списка»: позиция в файле и позиция в очереди — разные вещи.
    contentsList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    contentsList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(contentsList_, &QListWidget::itemDoubleClicked,
            this, &PlaylistEditor::onItemActivated);
    connect(contentsList_, &QListWidget::customContextMenuRequested,
            this, [this](const QPoint &pos) { showContentsMenu(pos); });

    // Delete/Backspace удаляют позицию из сохранённого списка, но только когда фокус
    // внутри этого списка: на весь стек браузеров эти клавиши перехватывать нельзя.
    for (const QKeySequence &seq : {QKeySequence::Delete, QKeySequence::Backspace}) {
        auto *shortcut = new QShortcut(seq, contentsList_);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated,
                this, &PlaylistEditor::removeCurrentPosition);
    }

    updateButtonStates();
}

// ── BrowserWidget interface ─────────────────────────────────────────────────

void PlaylistEditor::onConnectionChanged(bool connected)
{
    setEnabled(connected);
    if (connected) {
        refreshPlaylists();
    } else {
        playlistList_->clear();
        contentsList_->clear();
    }
    updateButtonStates();
}

QVector<BrowserIntent> PlaylistEditor::selectedIntents() const
{
    QVector<BrowserIntent> intents;
    // selectedItems(), а не выделение модели: позиции списка — QListWidgetItem, у них
    // нет индексов в смысле QAbstractItemView.
    for (QListWidgetItem *item : contentsList_->selectedItems()) {
        const QString uri = item->data(UriRole).toString();
        if (uri.isEmpty())
            continue;

        BrowserIntent intent;
        intent.kind = BrowserIntent::Kind::Song;
        intent.uri = uri;
        intents.append(intent);
    }
    return intents;
}

QList<int> PlaylistEditor::removalRows() const
{
    QList<int> rows;
    for (QListWidgetItem *item : contentsList_->selectedItems())
        rows.append(contentsList_->row(item));
    if (rows.isEmpty() && contentsList_->currentItem())
        rows.append(contentsList_->row(contentsList_->currentItem()));

    // Номера идут убывающими: `playlistdelete` перенумеровывает список, и после удаления
    // нулевой строки бывшая вторая стоит уже под первым номером — при восходящем порядке
    // удалился бы не тот трэк (замер на MPD 0.25).
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    return rows;
}

// ── Slots ───────────────────────────────────────────────────────────────────

void PlaylistEditor::onPlaylistSelected(int row)
{
    auto *item = playlistList_->item(row);
    if (item)
        refreshContents(item->text());
    else
        contentsList_->clear();
    updateButtonStates();
}

void PlaylistEditor::onNewPlaylist()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("New Playlist"), tr("Playlist name:"),
        QLineEdit::Normal, {}, &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    // Create empty playlist by saving the current queue — but we
    // don't want side effects.  Instead add a dummy song?  MPD
    // requires at least one song to create a playlist.  We'll try
    // to save an empty queue and catch the error silently.
    mpd_run_save(conn_->raw(), name.trimmed().toUtf8().constData());
    // consumeError() обязателен и при успехе: он завершает ответ. При неудаче —
    // выводит соединение из error-state, иначе встанут все последующие команды.
    const QString detail = conn_->consumeError("save");
    if (!detail.isEmpty()) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Could not create playlist. "
                                "Make sure the queue is not empty.\n%1").arg(detail));
    }

    refreshPlaylists();
}

void PlaylistEditor::onRenamePlaylist()
{
    auto *cur = playlistList_->currentItem();
    if (!cur) return;

    bool ok = false;
    const QString newName = QInputDialog::getText(
        this, tr("Rename Playlist"),
        tr("New name for \"%1\":").arg(cur->text()),
        QLineEdit::Normal, cur->text(), &ok);
    if (!ok || newName.trimmed().isEmpty())
        return;

    mpd_run_rename(conn_->raw(),
                   cur->text().toUtf8().constData(),
                   newName.trimmed().toUtf8().constData());
    conn_->consumeError("rename");
    refreshPlaylists();
}

void PlaylistEditor::onDeletePlaylist()
{
    auto *cur = playlistList_->currentItem();
    if (!cur) return;

    auto ret = QMessageBox::question(
        this, tr("Delete Playlist"),
        tr("Delete \"%1\"?").arg(cur->text()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    mpd_run_rm(conn_->raw(), cur->text().toUtf8().constData());
    conn_->consumeError("rm");
    refreshPlaylists();
}

void PlaylistEditor::onLoadPlaylist()
{
    auto *cur = playlistList_->currentItem();
    if (!cur) return;

    mpd_run_load(conn_->raw(), cur->text().toUtf8().constData());
    conn_->consumeError("load");
}

void PlaylistEditor::onItemActivated(QListWidgetItem *item)
{
    if (!item)
        return;

    const QString uri = item->data(UriRole).toString();
    if (!uri.isEmpty())
        cmds_->activateUri(uri);
}

void PlaylistEditor::removeCurrentPosition()
{
    const QString name = currentPlaylistName();
    if (name.isEmpty() || !conn_ || !conn_->isConnected())
        return;

    // Порядок убывающий — см. removalRows(): там причина, по которой не восходящий.
    const QList<int> rows = removalRows();
    if (rows.isEmpty())
        return;

    for (int row : rows)
        cmds_->deletePlaylistPosition(name, row);

    // Файл на сервере уже изменился, поэтому список перечитываем один раз после всех
    // удалений: ждать idle-событие — значит показывать строки, которых больше нет.
    refreshContents(name);
}

// ── Private ─────────────────────────────────────────────────────────────────

QString PlaylistEditor::currentPlaylistName() const
{
    auto *cur = playlistList_->currentItem();
    return cur ? cur->text() : QString();
}

void PlaylistEditor::showContentsMenu(const QPoint &pos)
{
    auto *item = contentsList_->itemAt(pos);
    if (!item)
        return;   // меню на пустом месте под строками показывать нечего

    // Правый клик по строке вне выделения должен относиться к ней, а не к тому, что
    // было выделено до этого (как в оригинальном редакторе). Выделение ставится явно:
    // полагаться на то, что setCurrentItem «обычно ещё и выделяет», значит получить
    // меню без единого доступного пункта при другом стиле оформления.
    if (!item->isSelected()) {
        contentsList_->clearSelection();
        item->setSelected(true);
        contentsList_->setCurrentItem(item);
    }

    const QVector<BrowserIntent> intents = selectedIntents();
    const QString musicDir = Config::instance().musicDirectory();
    const BrowserMenuActions actions = menuActionsFor(intents, musicDir);

    QMenu menu(contentsList_);
    QAction *play = menu.addAction(tr("Play"));
    QAction *add = menu.addAction(tr("Add to Queue"));
    play->setEnabled(actions.canPlay && conn_->isConnected());
    add->setEnabled(actions.canAdd && conn_->isConnected());
    const BrowserPathMenuItems paths = addPathMenuItems(menu, intents, musicDir);
    menu.addSeparator();
    QAction *remove = menu.addAction(tr("Remove from Playlist"));
    remove->setEnabled(!currentPlaylistName().isEmpty() && conn_->isConnected());

    QAction *chosen = menu.exec(contentsList_->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == play) {
        onItemActivated(item);
    } else if (chosen == add) {
        for (const BrowserIntent &intent : intents)
            cmds_->addUri(intent.uri);
    } else if (paths.openFolder && chosen == paths.openFolder) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(paths.folder));
    } else if (paths.copyPath && chosen == paths.copyPath) {
        QApplication::clipboard()->setText(copyTextFor(intents, musicDir));
    } else if (chosen == remove) {
        removeCurrentPosition();
    }
}

void PlaylistEditor::showNamesMenu(const QPoint &pos)
{
    auto *item = playlistList_->itemAt(pos);
    // Меню на пустом месте — это меню про «вообще ничего»: единственный осмысленный
    // пункт там «New», и он есть на панели кнопок.
    if (!item)
        return;

    if (!playlistList_->selectedItems().contains(item)) {
        playlistList_->clearSelection();
        item->setSelected(true);
        playlistList_->setCurrentItem(item);
    }

    const bool connected = conn_ && conn_->isConnected();

    QMenu menu(playlistList_);
    QAction *load = menu.addAction(tr("Load into Queue"));
    QAction *replace = menu.addAction(tr("Replace Queue with Playlist"));
    load->setEnabled(connected);
    replace->setEnabled(connected);
    menu.addSeparator();
    QAction *newPlaylist = menu.addAction(tr("New Playlist…"));
    newPlaylist->setEnabled(connected);
    QAction *rename = menu.addAction(tr("Rename Playlist…"));
    rename->setEnabled(connected);
    QAction *del = menu.addAction(tr("Delete Playlist…"));
    del->setEnabled(connected);

    QAction *chosen = menu.exec(playlistList_->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    // Действия над именем уже читают currentItem панели, а он выставлен выше.
    if (chosen == load)
        onLoadPlaylist();
    else if (chosen == replace)
        replaceQueueWithPlaylist();
    else if (chosen == newPlaylist)
        onNewPlaylist();
    else if (chosen == rename)
        onRenamePlaylist();
    else if (chosen == del)
        onDeletePlaylist();
}

void PlaylistEditor::replaceQueueWithPlaylist()
{
    const QString name = currentPlaylistName();
    if (name.isEmpty() || !conn_ || !conn_->isConnected())
        return;

    // Ровно как в оригинальном playlist_editor_replace_playlist: очистить очередь,
    // загрузить список (load добавляет в конец, поэтому очистка обязательна) и играть.
    cmds_->clearQueue();
    cmds_->loadStoredPlaylist(name);
    cmds_->play();
}

void PlaylistEditor::refreshPlaylists()
{
    playlistList_->blockSignals(true);
    playlistList_->clear();

    if (!conn_ || !conn_->isConnected()) {
        playlistList_->blockSignals(false);
        return;
    }

    {
        // Ответ должен быть завершён на любой ветке — раньше `goto done`
        // пропускал mpd_response_finish() и соединение оставалось в error-state.
        Connection::ResponseScope scope(*conn_, "listplaylists");

        if (mpd_send_list_playlists(conn_->raw())) {
            while (auto *pl = mpd_recv_playlist(conn_->raw())) {
                playlistList_->addItem(
                    QString::fromUtf8(mpd_playlist_get_path(pl)));
                mpd_playlist_free(pl);
            }
        }
    }

    playlistList_->blockSignals(false);
    contentsList_->clear();
    updateButtonStates();
}

void PlaylistEditor::refreshContents(const QString &playlistName)
{
    contentsList_->clear();

    if (!conn_ || !conn_->isConnected())
        return;

    Connection::ResponseScope scope(*conn_, "listplaylist");

    if (!mpd_send_list_playlist_meta(
            conn_->raw(), playlistName.toUtf8().constData()))
        return;

    while (auto *entity = conn_->nextEntity()) {
        if (mpd_entity_get_type(entity) == MPD_ENTITY_TYPE_SONG) {
            auto *song = mpd_entity_get_song(entity);
            const QString uri = QString::fromUtf8(mpd_song_get_uri(song));
            const QString artist = QString::fromUtf8(
                mpd_song_get_tag(song, MPD_TAG_ARTIST, 0));
            const QString title = QString::fromUtf8(
                mpd_song_get_tag(song, MPD_TAG_TITLE, 0));

            QString label;
            if (!artist.isEmpty() && !title.isEmpty())
                label = artist + QStringLiteral(" - ") + title;
            else
                label = uri;

            // URI рядом с подписью: активации и удалению позиции нужен именно файл,
            // а подпись «artist - title» к нему не привязана.
            auto *item = new QListWidgetItem(label, contentsList_);
            item->setData(UriRole, uri);
        }
        mpd_entity_free(entity);
    }
}

void PlaylistEditor::updateButtonStates()
{
    bool enabled = isEnabled();
    bool hasSelection = playlistList_->currentItem() != nullptr;

    newBtn_->setEnabled(enabled);
    renameBtn_->setEnabled(enabled && hasSelection);
    deleteBtn_->setEnabled(enabled && hasSelection);
    loadBtn_->setEnabled(enabled && hasSelection);
}
