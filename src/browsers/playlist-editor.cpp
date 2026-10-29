/**
 * @file playlist-editor.cpp
 * @brief MPD stored-playlist editor.
 */

#include "playlist-editor.h"
#include "core/connection.h"

#include <mpd/playlist.h>
#include <mpd/song.h>
#include <mpd/entity.h>

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

// ── Construction ────────────────────────────────────────────────────────────

PlaylistEditor::PlaylistEditor(Connection *conn, QWidget *parent)
    : BrowserWidget(parent)
    , conn_(conn)
    , playlistList_(new QListWidget(this))
    , contentsList_(new QListWidget(this))
    , newBtn_(new QPushButton(tr("New"), this))
    , renameBtn_(new QPushButton(tr("Rename"), this))
    , deleteBtn_(new QPushButton(tr("Delete"), this))
    , loadBtn_(new QPushButton(tr("Load"), this))
{
    auto *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);

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

    setEnabled(false);
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
    if (!mpd_run_save(conn_->raw(), name.trimmed().toUtf8().constData())) {
        // If it fails (e.g. no queue), show an error
        QMessageBox::warning(this, tr("Error"),
                             tr("Could not create playlist. "
                                "Make sure the queue is not empty."));
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
    refreshPlaylists();
}

void PlaylistEditor::onLoadPlaylist()
{
    auto *cur = playlistList_->currentItem();
    if (!cur) return;

    mpd_run_load(conn_->raw(), cur->text().toUtf8().constData());
}

// ── Private ─────────────────────────────────────────────────────────────────

void PlaylistEditor::refreshPlaylists()
{
    playlistList_->blockSignals(true);
    playlistList_->clear();

    if (!conn_ || !conn_->isConnected()) {
        playlistList_->blockSignals(false);
        return;
    }

    if (!mpd_send_list_playlists(conn_->raw()))
        goto done;

    while (auto *pl = mpd_recv_playlist(conn_->raw())) {
        playlistList_->addItem(
            QString::fromUtf8(mpd_playlist_get_path(pl)));
        mpd_playlist_free(pl);
    }
    mpd_response_finish(conn_->raw());

done:
    playlistList_->blockSignals(false);
    contentsList_->clear();
    updateButtonStates();
}

void PlaylistEditor::refreshContents(const QString &playlistName)
{
    contentsList_->clear();

    if (!conn_ || !conn_->isConnected())
        return;

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

            contentsList_->addItem(label);
        }
        mpd_entity_free(entity);
    }
    mpd_response_finish(conn_->raw());
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
