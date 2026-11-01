#include "progress-slider.h"
#include <QMouseEvent>
#include <QToolTip>

ProgressSlider::ProgressSlider(Qt::Orientation orientation, QWidget *parent)
    : QSlider(orientation, parent)
{
    setRange(0, 1000); // promille
    setMouseTracking(true);
}

void ProgressSlider::setPosition(float elapsed, float total)
{
    total_ = total;
    if (!seeking_ && total > 0)
        setValue(static_cast<int>(elapsed / total * 1000));
}

void ProgressSlider::setTotal(float total)
{
    total_ = total;
}

// ── Tooltip helper ──────────────────────────────────────────────────────────

static QString formatTime(float secs)
{
    if (secs <= 0) return QStringLiteral("0:00");
    int m = static_cast<int>(secs) / 60;
    int s = static_cast<int>(secs) % 60;
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
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
        val = qBound(minimum(), val, maximum());
        setValue(val);

        float secs = total_ * val / 1000.0f;
        QToolTip::showText(event->globalPosition().toPoint(),
                           formatTime(secs) + QStringLiteral(" / ") + formatTime(total_),
                           this);
    }
}

void ProgressSlider::mouseReleaseEvent(QMouseEvent *event)
{
    seeking_ = false;
    QSlider::mouseReleaseEvent(event);
    // Значение шкалы — промилле; наружу идут секунды, иначе seekcur получил бы
    // «1000 секунд» вместо конца трека.
    if (maximum() > 0 && total_ > 0)
        emit seekRequested(static_cast<int>(total_ * value() / maximum() + 0.5f));
}
