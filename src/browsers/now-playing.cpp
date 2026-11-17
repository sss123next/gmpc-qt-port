/**
 * @file now-playing.cpp
 * @brief Full-page «Now Playing» screen — large cover art and song lyrics.
 */

#include "now-playing.h"
#include "widgets/cover-widget.h"
#include "widgets/cover-viewer.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

#include "core/playlist-model.h"
#include "metadata/metadata-manager.h"
#include "widgets/cover-widget.h"

NowPlaying::NowPlaying(QWidget *parent)
    : BrowserWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 12, 12, 12);

    // ── Верх: крупная обложка слева, теги справа (как в оригинале — картинка и текст) ─
    auto *topRow = new QHBoxLayout;
    cover_ = new CoverWidget(this);
    cover_->setCoverSize(256);   // странице не тесно: обложку рисуем крупно
    topRow->addWidget(cover_, 0, Qt::AlignTop);

    auto *infoCol = new QVBoxLayout;
    // objectName — чтобы тест находил строки, не полагаясь на порядок потомков.
    titleLabel_ = new QLabel(this);
    titleLabel_->setObjectName(QStringLiteral("titleLabel"));
    artistLabel_ = new QLabel(this);
    artistLabel_->setObjectName(QStringLiteral("artistLabel"));
    albumLabel_ = new QLabel(this);
    albumLabel_->setObjectName(QStringLiteral("albumLabel"));
    // Заголовок крупнее остальных строк: это первое, что читают на странице.
    titleLabel_->setStyleSheet(QStringLiteral("font-size: 20pt; font-weight: bold;"));
    artistLabel_->setStyleSheet(QStringLiteral("font-size: 13pt;"));
    for (QLabel *l : {titleLabel_, artistLabel_, albumLabel_}) {
        l->setWordWrap(true);
        infoCol->addWidget(l, 0, Qt::AlignLeft | Qt::AlignTop);
    }
    hintLabel_ = new QLabel(this);
    hintLabel_->setObjectName(QStringLiteral("hintLabel"));
    infoCol->addStretch(1);
    topRow->addLayout(infoCol, 1);

    lay->addLayout(topRow);

    // ── Низ: текст песни. Вид только для чтения — правки лирику не сохраняют ─────
    lyricsView_ = new QPlainTextEdit(this);
    lyricsView_->setObjectName(QStringLiteral("lyricsView"));
    lyricsView_->setReadOnly(true);
    lyricsView_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    lay->addWidget(lyricsView_, 1);

    hintLabel_->setStyleSheet(
        QStringLiteral("color: palette(disabled, text);"));
    lay->addWidget(hintLabel_);

    showNothing();

    // Click on cover → full-screen viewer.
    connect(cover_, &CoverWidget::clicked, this, [this] {
        const QPixmap pm = cover_->pixmap();
        if (!pm.isNull()) {
            auto *viewer = new CoverViewer();
            viewer->show(pm);
        }
    });
}

void NowPlaying::setMetadataManager(MetadataManager *mgr)
{
    mgr_ = mgr;
    if (!mgr_)
        return;

    // CoverWidget тоже должен знать о менеджере, иначе fetchCover() чистит обложку.
    cover_->setMetadataManager(mgr_);

    connect(mgr_, &MetadataManager::lyricsReady,
            this, &NowPlaying::onLyricsReady);
    connect(mgr_, &MetadataManager::lyricsNotFound,
            this, &NowPlaying::onLyricsNotFound);

    // Трэк мог быть показан до появления менеджера — дотягиваем лирику теперь.
    if (!curArtist_.isEmpty())
        mgr_->requestLyrics(curArtist_, curTitle_);
}

QString NowPlaying::lyricsKey(const QString &artist, const QString &title)
{
    return artist + QLatin1Char('\n') + title;
}

void NowPlaying::showNothing()
{
    cover_->clear();
    titleLabel_->clear();
    artistLabel_->clear();
    albumLabel_->clear();
    lyricsView_->clear();
    // Сообщение уходит в строку состояния: крупный заголовок под пустой трэк не врём.
    hintLabel_->setText(tr("Nothing is playing"));
    curArtist_.clear();
    curTitle_.clear();
}

void NowPlaying::onConnectionChanged(bool connected)
{
    BrowserWidget::onConnectionChanged(connected);
    if (!connected) {
        // Очередь ушла вместе с соединением: держать на экране прошлый трэк нельзя.
        showNothing();
    }
}

void NowPlaying::setSong(const SongEntry *song)
{
    if (!song) {
        showNothing();
        return;
    }

    // Без тега Title показываем имя файла — как это делает очередь для своей колонки.
    const QString name = song->title.isEmpty()
                             ? QFileInfo(song->uri).fileName()
                             : song->title;

    titleLabel_->setText(name);
    artistLabel_->setText(song->artist);
    albumLabel_->setText(song->album);

    if (song->artist.isEmpty() || song->album.isEmpty())
        cover_->clear();
    else
        cover_->fetchCover(song->artist, song->album, song->uri);

    curArtist_ = song->artist;
    curTitle_  = name;

    lyricsView_->clear();
    if (!mgr_ || curArtist_.isEmpty()) {
        hintLabel_->setText(tr("No lyrics"));
        return;
    }
    // Ключ запоминается до ответа: поздний текст по прошлому трэку отсеет слот.
    hintLabel_->setText(tr("Loading lyrics\u2026"));
    mgr_->requestLyrics(curArtist_, curTitle_);
}

void NowPlaying::onLyricsReady(const QString &artist, const QString &title,
                               const QString &lyrics)
{
    // Строгое равенство: после «ничего не играет» ключ пуст, и поздний ответ
    // прошлого трэка на экран уже попасть не должен.
    if (lyricsKey(artist, title) != lyricsKey(curArtist_, curTitle_))
        return;
    lyricsView_->setPlainText(lyrics);
    hintLabel_->clear();
}

void NowPlaying::onLyricsNotFound(const QString &artist, const QString &title)
{
    if (lyricsKey(artist, title) != lyricsKey(curArtist_, curTitle_))
        return;
    lyricsView_->setPlainText({});
    hintLabel_->setText(tr("No lyrics found"));
}