#pragma once

/**
 * @file track-info-window.h
 * @brief Окно «Track Info» — обложка, метаданные и лирика выбранного трэка.
 *
 * Повторно использует TrackInfoPanel — тот же визуал, что и Now Playing,
 * но для произвольного трэка (не только текущего в очереди).
 */

#include <QDialog>

#include "core/playlist-model.h"

class Connection;
class MetadataManager;
class TrackInfoPanel;

/// Окно метаданных выбранного трэка, идентичное Now Playing по оформлению.
class TrackInfoWindow : public QDialog {
    Q_OBJECT
public:
    /** @param song   теги трэка; будет дозапрошены полные теги через conn
     *  @param conn   соединение для MPD-запросов (может быть nullptr — только локальные теги)
     *  @param mgr    менеджер метаданных для обложки и лирики (может быть nullptr)
     *  @param parent родительское окно */
    explicit TrackInfoWindow(const SongEntry &song,
                             Connection *conn,
                             MetadataManager *mgr,
                             QWidget *parent = nullptr);

private:
    TrackInfoPanel *panel_;
};
