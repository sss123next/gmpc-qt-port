/**
 * @file playlist-editor.cpp
 * @brief MPD stored-playlist editor.
 */

#include "playlist-editor.h"
#include "core/connection.h"
#include "core/mpd-commands.h"

#include <mpd/playlist.h>
#include <mpd/song.h>
#include <mpd/entity.h>

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QVBoxLayout>

namespace {
/// URI трэка в позиции списка воспроизведения (роль данных QListWidgetItem).
constexpr int kUriRole = Qt::UserRole + 1;
}

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

    // ── позиции сохранённого списка ──
    // Двойной клик — как row-activated в оригинале: play_path(), а не «играть N-ю
    // строку списка»: позиция в файле и позиция в очереди — разные вещи.
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

    const QString uri = item->data(kUriRole).toString();
    if (!uri.isEmpty())
        cmds_->activateUri(uri);
}

void PlaylistEditor::removeCurrentPosition()
{
    auto *item = contentsList_->currentItem();
    const QString name = currentPlaylistName();
    if (!item || name.isEmpty())
        return;

    // Строка списка — это и есть позиция в сохранённом файле: MPD нумерует их с нуля,
    // ровно как QListWidget.
    cmds_->deletePlaylistPosition(name, contentsList_->row(item));

    // Файл на сервере уже изменился, поэтому список перечитываем сразу: ждать
    // idle-событие — значит показывать строку, которой больше нет.
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

    contentsList_->setCurrentItem(item);

    QMenu menu(contentsList_);
    QAction *play = menu.addAction(tr("Play"));
    QAction *remove = menu.addAction(tr("Remove from playlist"));
    play->setEnabled(!item->data(kUriRole).toString().isEmpty());
    remove->setEnabled(!currentPlaylistName().isEmpty() && conn_->isConnected());

    QAction *chosen = menu.exec(contentsList_->viewport()->mapToGlobal(pos));
    if (chosen == play)
        onItemActivated(item);
    else if (chosen == remove)
        removeCurrentPosition();
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
            item->setData(kUriRole, uri);
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
