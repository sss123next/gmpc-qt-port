#pragma once

/**
 * @file cover-widget.h
 * @brief Album-art display widget.
 *
 * Fetches cover art asynchronously via AsyncDownload and displays
 * the result as a scaled QPixmap.  Supports clearing and manual
 * setCover() for cached images.
 */

#include <QLabel>
#include "tools/async-download.h"

/// Album-cover image label with async fetching.
class CoverWidget : public QLabel {
    Q_OBJECT
public:
    explicit CoverWidget(QWidget *parent = nullptr);
    /// Fetch cover art from an online provider for artist/album.
    void fetchCover(const QString &artist, const QString &album);
    /// Immediately display a pre-loaded pixmap.
    void setCover(const QPixmap &pixmap);
    /// Clear the displayed cover.
    void clear();

private:
    AsyncDownload *dl_;   ///< HTTP download helper.
};

