#pragma once

/**
 * @file controlbar.h
 * @brief Transport control bar.
 *
 * Displays play/stop/prev/next buttons, repeat/random/single/consume
 * toggles, volume slider, seek slider, time label, and current-song
 * label.  Updated via MPD status push from ControlBar::updateStatus().
 *
 * CoverWidget and VolumeWidget are created and owned by MainWindow,
 * injected here for layout placement.
 */

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QIcon>
#include "core/connection.h"

class MpdCommands;
class CoverWidget;
class ProgressSlider;
class SongInfo;
class VolumeWidget;

/// Horizontal bar of MPD transport and mode controls.
class ControlBar : public QWidget {
    Q_OBJECT
public:
    /// @param cmds   Transport commands.
    /// @param cover  Cover-widget to display in the bar (owned by caller).
    /// @param info   Song-info panel to display (owned by caller).
    /// @param vol    Volume widget to display (owned by caller).
    /// @param parent Qt parent.
    explicit ControlBar(MpdCommands *cmds, CoverWidget *cover, SongInfo *info,
                        VolumeWidget *vol, QWidget *parent = nullptr);

    /// Refresh all widgets from a fresh MPD status snapshot.
    void updateStatus(const Connection::Status &s);

private:
    MpdCommands *cmds_;
    QPushButton *playBtn_, *stopBtn_, *prevBtn_, *nextBtn_;
    QPushButton *repeatBtn_, *randomBtn_, *singleBtn_, *consumeBtn_;
    QPushButton *countdownBtn_;
    ProgressSlider *seekSlider_;   ///< Прогресс + перемотка (сигнал в секундах)
    QLabel *timeLabel_;
    QIcon playIcon_, pauseIcon_; ///< Pre-created to avoid standardIcon() per tick.
    bool countdown_ = false;     ///< Show remaining time instead of elapsed.
};

