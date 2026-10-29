#ifndef GMPCQT_SONG_INFO_H
#define GMPCQT_SONG_INFO_H

/**
 * @file song-info.h
 * @brief Current-song display panel.
 *
 * Shows artist, title, and album labels for the currently playing song.
 */

#include <QWidget>
#include <QLabel>
#include "core/playlist-model.h"

/// Compact artist / title / album readout.
class SongInfo : public QWidget {
    Q_OBJECT
public:
    explicit SongInfo(QWidget *parent = nullptr);
    /// Populate labels from a SongEntry.
    void showSong(const SongEntry *song);
    /// Clear all labels.
    void clear();

private:
    QLabel *artistLabel_;  ///< Artist name.
    QLabel *titleLabel_;   ///< Song title.
    QLabel *albumLabel_;   ///< Album name.
};

#endif // GMPCQT_SONG_INFO_H
