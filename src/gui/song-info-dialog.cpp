/**
 * @file song-info-dialog.cpp
 * @brief Окно «Song Info» — метаданные выбранного трэка и текст песни.
 */

#include "song-info-dialog.h"

#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

#include "metadata/metadata-manager.h"

namespace {

/// Строка «подпись: значение» в форме окна.
void addRow(QFormLayout *form, const QString &caption, QLabel **out)
{
    auto *value = new QLabel;
    value->setObjectName(caption.toLower());
    // Длинные теги (путь) переносим, а не растягиваем окно за экран.
    value->setWordWrap(true);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(caption + QLatin1Char(':'), value);
    *out = value;
}

} // namespace

SongInfoDialog::SongInfoDialog(const SongEntry &song, MetadataManager *mgr,
                               QWidget *parent)
    : QDialog(parent), mgr_(mgr)
{
    setWindowTitle(tr("Song Info"));
    resize(480, 420);

    auto *lay = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);

    // Без тега Title показываем имя файла — как очередь и страница Now Playing.
    const QString name = song.title.isEmpty() ? QFileInfo(song.uri).fileName()
                                              : song.title;

    QLabel *titleValue = nullptr;
    addRow(form, tr("Title"), &titleValue);
    titleValue->setText(name);

    QLabel *artistValue = nullptr;
    addRow(form, tr("Artist"), &artistValue);
    artistValue->setText(song.artist.isEmpty() ? QStringLiteral("\u2014") : song.artist);

    QLabel *albumValue = nullptr;
    addRow(form, tr("Album"), &albumValue);
    albumValue->setText(song.album.isEmpty() ? QStringLiteral("\u2014") : song.album);

    QLabel *lengthValue = nullptr;
    addRow(form, tr("Length"), &lengthValue);
    lengthValue->setText(song.duration > 0
                             ? QStringLiteral("%1:%2")
                                   .arg(song.duration / 60)
                                   .arg(song.duration % 60, 2, 10, QLatin1Char('0'))
                             : tr("unknown"));

    QLabel *pathValue = nullptr;
    addRow(form, tr("Path"), &pathValue);
    pathValue->setText(song.uri);

    lay->addLayout(form);

    // ── Лирика: вид только для чтения и строка состояния под ним ─────────────
    lyricsView_ = new QPlainTextEdit(this);
    lyricsView_->setObjectName(QStringLiteral("lyricsView"));
    lyricsView_->setReadOnly(true);
    lay->addWidget(lyricsView_, 1);

    hintLabel_ = new QLabel(this);
    hintLabel_->setObjectName(QStringLiteral("hintLabel"));
    hintLabel_->setStyleSheet(QStringLiteral("color: palette(disabled, text);"));
    lay->addWidget(hintLabel_);

    curArtist_ = song.artist;
    curTitle_  = name;

    if (!mgr_) {
        // Менеджера нет — сеть не дёргается вовсе.
        hintLabel_->setText(tr("No lyrics"));
        return;
    }

    connect(mgr_, &MetadataManager::lyricsReady,
            this, &SongInfoDialog::onLyricsReady);
    connect(mgr_, &MetadataManager::lyricsNotFound,
            this, &SongInfoDialog::onLyricsNotFound);

    if (curArtist_.isEmpty()) {
        hintLabel_->setText(tr("No lyrics"));
        return;
    }
    // Ключ запоминается до ответа: поздний текст по прошлому выбору отсеет слот.
    hintLabel_->setText(tr("Loading lyrics\u2026"));
    mgr_->requestLyrics(curArtist_, curTitle_);
}

QString SongInfoDialog::lyricsKey(const QString &artist, const QString &title)
{
    return artist + QLatin1Char('\n') + title;
}

void SongInfoDialog::onLyricsReady(const QString &artist, const QString &title,
                                   const QString &lyrics)
{
    if (lyricsKey(artist, title) != lyricsKey(curArtist_, curTitle_))
        return;
    lyricsView_->setPlainText(lyrics);
    hintLabel_->clear();
}

void SongInfoDialog::onLyricsNotFound(const QString &artist, const QString &title)
{
    if (lyricsKey(artist, title) != lyricsKey(curArtist_, curTitle_))
        return;
    lyricsView_->setPlainText({});
    hintLabel_->setText(tr("No lyrics found"));
}