#include "progress-slider.h"
#include <QMouseEvent>

ProgressSlider::ProgressSlider(Qt::Orientation orientation, QWidget *parent)
    : QSlider(orientation, parent)
{
    setRange(0, 1000); // promille
}

void ProgressSlider::setPosition(float elapsed, float total)
{
    if (!seeking_ && total > 0)
        setValue(static_cast<int>(elapsed / total * 1000));
}

void ProgressSlider::setTotal(float total)
{
    // no-op, range is normalized
}

void ProgressSlider::mousePressEvent(QMouseEvent *event)
{
    seeking_ = true;
    QSlider::mousePressEvent(event);
}

void ProgressSlider::mouseMoveEvent(QMouseEvent *event)
{
    if (seeking_) {
        int val = minimum() + (maximum() - minimum()) * event->position().x() / width();
        setValue(qBound(minimum(), val, maximum()));
    }
}

void ProgressSlider::mouseReleaseEvent(QMouseEvent *event)
{
    seeking_ = false;
    QSlider::mouseReleaseEvent(event);
    if (maximum() > 0)
        emit seekRequested(value());
}
