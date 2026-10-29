#include "song-info.h"
#include <QVBoxLayout>
#include <QFont>

SongInfo::SongInfo(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    artistLabel_ = new QLabel(this);
    QFont f = artistLabel_->font();
    f.setBold(true);
    f.setPointSize(f.pointSize() + 2);
    artistLabel_->setFont(f);

    titleLabel_ = new QLabel(this);
    albumLabel_ = new QLabel(this);
    albumLabel_->setStyleSheet("color: gray;");

    lay->addWidget(artistLabel_);
    lay->addWidget(titleLabel_);
    lay->addWidget(albumLabel_);
}

void SongInfo::showSong(const SongEntry *song)
{
    if (!song) { clear(); return; }
    artistLabel_->setText(song->artist);
    titleLabel_->setText(song->title);
    albumLabel_->setText(song->album);
}

void SongInfo::clear()
{
    artistLabel_->clear();
    titleLabel_->clear();
    albumLabel_->clear();
}
