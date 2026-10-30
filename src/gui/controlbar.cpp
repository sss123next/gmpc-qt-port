#include "controlbar.h"
#include "core/mpd-commands.h"
#include "widgets/cover-widget.h"
#include "widgets/song-info.h"
#include "widgets/volume-widget.h"

#include <QHBoxLayout>
#include <QStyle>

ControlBar::ControlBar(MpdCommands *cmds, CoverWidget *cover, SongInfo *info,
                       VolumeWidget *vol, QWidget *parent)
    : QWidget(parent), cmds_(cmds)
{
    // Pre-create icons once to avoid standardIcon() call on every status tick.
    playIcon_  = style()->standardIcon(QStyle::SP_MediaPlay);
    pauseIcon_ = style()->standardIcon(QStyle::SP_MediaPause);

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);

    // — Transport buttons —
    prevBtn_ = new QPushButton(this);
    prevBtn_->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
    connect(prevBtn_, &QPushButton::clicked, cmds_, &MpdCommands::previous);

    playBtn_ = new QPushButton(this);
    playBtn_->setIcon(playIcon_);
    playBtn_->setCheckable(true);
    connect(playBtn_, &QPushButton::clicked, cmds_, &MpdCommands::togglePause);

    stopBtn_ = new QPushButton(this);
    stopBtn_->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    connect(stopBtn_, &QPushButton::clicked, cmds_, &MpdCommands::stop);

    nextBtn_ = new QPushButton(this);
    nextBtn_->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));
    connect(nextBtn_, &QPushButton::clicked, cmds_, &MpdCommands::next);

    // — Seek slider + time —
    seekSlider_ = new QSlider(Qt::Horizontal, this);
    seekSlider_->setRange(0, 0);
    seekSlider_->setMaximumWidth(300);

    timeLabel_ = new QLabel("0:00 / 0:00", this);

    // — Mode buttons —
    repeatBtn_ = new QPushButton(tr("R"), this);
    repeatBtn_->setCheckable(true);
    connect(repeatBtn_, &QPushButton::clicked, cmds_, &MpdCommands::toggleRepeat);

    randomBtn_ = new QPushButton(tr("Z"), this);
    randomBtn_->setCheckable(true);
    connect(randomBtn_, &QPushButton::clicked, cmds_, &MpdCommands::toggleRandom);

    singleBtn_ = new QPushButton(tr("1"), this);
    singleBtn_->setCheckable(true);
    connect(singleBtn_, &QPushButton::clicked, cmds_, &MpdCommands::toggleSingle);

    consumeBtn_ = new QPushButton(tr("C"), this);
    consumeBtn_->setCheckable(true);
    connect(consumeBtn_, &QPushButton::clicked, cmds_, &MpdCommands::toggleConsume);

    // — Layout: [buttons] [seek+time] [cover] [song-info] [stretch] [modes] [vol] —
    lay->addWidget(prevBtn_);
    lay->addWidget(playBtn_);
    lay->addWidget(stopBtn_);
    lay->addWidget(nextBtn_);
    lay->addWidget(seekSlider_);
    lay->addWidget(timeLabel_);
    lay->addWidget(cover);
    lay->addWidget(info);
    lay->addStretch();
    lay->addWidget(repeatBtn_);
    lay->addWidget(randomBtn_);
    lay->addWidget(singleBtn_);
    lay->addWidget(consumeBtn_);
    lay->addWidget(vol);

    setMaximumHeight(48);
}

void ControlBar::updateStatus(const Connection::Status &s)
{
    playBtn_->setChecked(s.state == MPD_STATE_PLAY);
    playBtn_->setIcon(s.state == MPD_STATE_PLAY ? pauseIcon_ : playIcon_);

    if (s.totalDuration > 0) {
        seekSlider_->setRange(0, static_cast<int>(s.totalDuration));
        seekSlider_->setValue(static_cast<int>(s.elapsed));
    }
    timeLabel_->setText(QStringLiteral("%1:%2 / %3:%4")
        .arg(static_cast<int>(s.elapsed) / 60, 2, 10, QChar('0'))
        .arg(static_cast<int>(s.elapsed) % 60, 2, 10, QChar('0'))
        .arg(static_cast<int>(s.totalDuration) / 60, 2, 10, QChar('0'))
        .arg(static_cast<int>(s.totalDuration) % 60, 2, 10, QChar('0')));

    repeatBtn_->setChecked(s.repeat);
    randomBtn_->setChecked(s.random);
    singleBtn_->setChecked(s.single);
    consumeBtn_->setChecked(s.consume);
}
