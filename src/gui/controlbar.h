#ifndef GMPCQT_CONTROLBAR_H
#define GMPCQT_CONTROLBAR_H

/**
 * @file controlbar.h
 * @brief Transport control bar.
 *
 * Displays play/stop/prev/next buttons, repeat/random/single/consume
 * toggles, volume slider, seek slider, time label, and current-song
 * label.  Updated via MPD status push from ControlBar::updateStatus().
 */

#include <QWidget>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include "core/connection.h"

class MpdCommands;

/// Horizontal bar of MPD transport and mode controls.
class ControlBar : public QWidget {
    Q_OBJECT
public:
    explicit ControlBar(MpdCommands *cmds, QWidget *parent = nullptr);

    /// Refresh all widgets from a fresh MPD status snapshot.
    void updateStatus(const Connection::Status &s);

private:
    MpdCommands *cmds_;
    QPushButton *playBtn_, *stopBtn_, *prevBtn_, *nextBtn_;
    QPushButton *repeatBtn_, *randomBtn_, *singleBtn_, *consumeBtn_;
    QSlider *volumeSlider_, *seekSlider_;
    QLabel *timeLabel_, *songLabel_;
};

#endif // GMPCQT_CONTROLBAR_H
