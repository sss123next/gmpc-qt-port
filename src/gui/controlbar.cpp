#include "controlbar.h"
#include "core/mpd-commands.h"

#include <QHBoxLayout>
#include <QStyle>

ControlBar::ControlBar(MpdCommands *cmds, QWidget *parent)
    : QWidget(parent), cmds_(cmds)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);

    // Transport buttons
    prevBtn_ = new QPushButton(this);
    prevBtn_->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
    connect(prevBtn_, &QPushButton::clicked, cmds_, &MpdCommands::previous);

    playBtn_ = new QPushButton(this);
    playBtn_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    playBtn_->setCheckable(true);
    connect(playBtn_, &QPushButton::clicked, cmds_, &MpdCommands::togglePause);

    stopBtn_ = new QPushButton(this);
    stopBtn_->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    connect(stopBtn_, &QPushButton::clicked, cmds_, &MpdCommands::stop);

    nextBtn_ = new QPushButton(this);
    nextBtn_->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));
    connect(nextBtn_, &QPushButton::clicked, cmds_, &MpdCommands::next);

    // Seek slider
    seekSlider_ = new QSlider(Qt::Horizontal, this);
    seekSlider_->setRange(0, 0);
    seekSlider_->setMaximumWidth(300);

    timeLabel_ = new QLabel("0:00 / 0:00", this);

    // Volume
    volumeSlider_ = new QSlider(Qt::Horizontal, this);
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(70);
    volumeSlider_->setMaximumWidth(100);
    connect(volumeSlider_, &QSlider::valueChanged, cmds_, &MpdCommands::setVolume);

    // Mode buttons
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

    // Song label (elided)
    songLabel_ = new QLabel(this);
    songLabel_->setMinimumWidth(200);

    lay->addWidget(prevBtn_);
    lay->addWidget(playBtn_);
    lay->addWidget(stopBtn_);
    lay->addWidget(nextBtn_);
    lay->addWidget(seekSlider_);
    lay->addWidget(timeLabel_);
    lay->addWidget(songLabel_);
    lay->addStretch();
    lay->addWidget(repeatBtn_);
    lay->addWidget(randomBtn_);
    lay->addWidget(singleBtn_);
    lay->addWidget(consumeBtn_);
    lay->addWidget(volumeSlider_);

    setMaximumHeight(40);
}

void ControlBar::updateStatus(const Connection::Status &s)
{
    playBtn_->setChecked(s.state == MPD_STATE_PLAY);
    playBtn_->setIcon(style()->standardIcon(
        s.state == MPD_STATE_PLAY ? QStyle::SP_MediaPause
                                      : QStyle::SP_MediaPlay));

    if (s.totalDuration > 0) {
        seekSlider_->setRange(0, static_cast<int>(s.totalDuration));
        seekSlider_->setValue(static_cast<int>(s.elapsed));
    }
    timeLabel_->setText(QStringLiteral("%1:%2 / %3:%4")
        .arg(static_cast<int>(s.elapsed) / 60, 2, 10, QChar('0'))
        .arg(static_cast<int>(s.elapsed) % 60, 2, 10, QChar('0'))
        .arg(static_cast<int>(s.totalDuration) / 60, 2, 10, QChar('0'))
        .arg(static_cast<int>(s.totalDuration) % 60, 2, 10, QChar('0')));

    volumeSlider_->setValue(s.volume);
    repeatBtn_->setChecked(s.repeat);
    randomBtn_->setChecked(s.random);
    singleBtn_->setChecked(s.single);
    consumeBtn_->setChecked(s.consume);
}
