#pragma once

/**
 * @file cover-widget.h
 * @brief Album-art display widget.
 *
 * Fetches cover art asynchronously via CoverProvider/MetadataManager
 * and displays the result as a scaled QPixmap.  Supports clearing
 * and manual setCover() for cached images.
 */

#include <QLabel>
#include <QString>

class MetadataManager;

/// Album-cover image label with async fetching.
class CoverWidget : public QLabel {
    Q_OBJECT
public:
    explicit CoverWidget(QWidget *parent = nullptr);

    /// Set the metadata manager to use for cover fetching.
    void setMetadataManager(MetadataManager *mgr);

    /** @brief Размер отрисовки обложки. По умолчанию 128 — размер control bar;
     *         страница «Now Playing» просит крупнее, места ей хватает. */
    void setCoverSize(int px);

    /// Fetch cover art for artist/album from the active pipeline.
    void fetchCover(const QString &artist, const QString &album);
    /// Immediately display a pre-loaded pixmap.
    void setCover(const QPixmap &pixmap);
    /// Clear the displayed cover.
    void clear();

private:
    MetadataManager *mgr_ = nullptr;
    int coverPx_ = 128;   ///< сторона квадрата отрисовки, px
};

