#ifndef GMPCQT_METADATA_MANAGER_H
#define GMPCQT_METADATA_MANAGER_H

/**
 * @file metadata-manager.h
 * @brief Orchestrator for metadata and cover-art retrieval.
 *
 * Coordinates CoverProvider, cache, and lyric fetchers.
 * TODO — full implementation.
 */

#include <QObject>

/// Central coordinator for metadata fetching and caching.
class MetadataManager : public QObject {
    Q_OBJECT
public:
    explicit MetadataManager(QObject *parent = nullptr);
};

#endif // GMPCQT_METADATA_MANAGER_H
