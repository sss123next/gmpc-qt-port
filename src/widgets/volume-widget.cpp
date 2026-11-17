#include "volume-widget.h"
#include <QHBoxLayout>
#include <algorithm>

VolumeWidget::VolumeWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    label_ = new QLabel("70%", this);
    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(0, 100);
    slider_->setValue(70);

    connect(slider_, &QSlider::valueChanged, this, [this](int v) {
        label_->setText(QStringLiteral("%1%").arg(v));
        emit volumeChanged(v);
    });

    lay->addWidget(slider_);
    lay->addWidget(label_);
}

void VolumeWidget::setVolume(int vol)
{
    slider_->setValue(std::clamp(vol, 0, 100));
}
