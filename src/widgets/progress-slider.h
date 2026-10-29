#ifndef GMPCQT_PROGRESS_SLIDER_H
#define GMPCQT_PROGRESS_SLIDER_H

/**
 * @file progress-slider.h
 * @brief Seekable playback progress slider.
 *
 * Displays song position / total duration.  The user can drag to seek;
 * the dragging state suppresses position updates until release.
 */

#include <QSlider>
#include <QElapsedTimer>

/// Custom QSlider for MPD playback position and seeking.
class ProgressSlider : public QSlider {
    Q_OBJECT
public:
    explicit ProgressSlider(Qt::Orientation orientation, QWidget *parent = nullptr);

    /// Update displayed position from MPD status.
    void setPosition(float elapsed, float total);
    /// Set total duration (seconds).
    void setTotal(float total);

signals:
    /// Emitted when the user releases the slider after a seek drag.
    void seekRequested(int seconds);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    bool seeking_ = false;  ///< True while the user is dragging.
};

#endif // GMPCQT_PROGRESS_SLIDER_H
