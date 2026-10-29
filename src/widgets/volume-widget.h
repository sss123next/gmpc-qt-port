#ifndef GMPCQT_VOLUME_WIDGET_H
#define GMPCQT_VOLUME_WIDGET_H

/**
 * @file volume-widget.h
 * @brief Volume control combining a slider and percentage label.
 */

#include <QWidget>
#include <QSlider>
#include <QLabel>

/// Horizontal volume slider with a "%" readout.
class VolumeWidget : public QWidget {
    Q_OBJECT
public:
    explicit VolumeWidget(QWidget *parent = nullptr);
    /// Set the displayed volume (clamped to 0–100).
    void setVolume(int vol);

signals:
    /// Emitted when the user drags the slider to a new value.
    void volumeChanged(int vol);

private:
    QSlider *slider_;
    QLabel *label_;
};

#endif // GMPCQT_VOLUME_WIDGET_H
