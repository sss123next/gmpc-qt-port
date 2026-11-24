#pragma once

/**
 * @file cover-widget.h
 * @brief Album-art display widget.
 *
 * Fetches cover art asynchronously via CoverProvider/MetadataManager
 * and displays the result as a scaled QPixmap.  Supports clearing
 * and manual setCover() for cached images.
 *
 * Custom-painted QWidget (not QLabel) to avoid pixmap/text rendering
 * artifacts that occur when switching between setPixmap and setText.
 */

#include <QWidget>
#include <QPixmap>
#include <QString>
#include <QByteArray>
#include <QMouseEvent>

class MetadataManager;

/// Album-cover image label with async fetching.
class CoverWidget : public QWidget {
    Q_OBJECT
public:
    explicit CoverWidget(QWidget *parent = nullptr);

    /// Set the metadata manager to use for cover fetching.
    void setMetadataManager(MetadataManager *mgr);

    /** @brief Размер отрисовки обложки. По умолчанию 128 — размер control bar;
     *         страница «Now Playing» просит крупнее, места ей хватает. */
    void setCoverSize(int px);

    /// Fetch cover art for artist/album from the active pipeline.
    /// @param uri  MPD song URI for local file lookup.
    void fetchCover(const QString &artist, const QString &album,
                    const QString &uri = QString());
    /// Immediately display a pre-loaded pixmap.
    void setCover(const QPixmap &pixmap);
    /// Clear the displayed cover.
    void clear();

    /** @brief Подпись-заглушка («No cover») либо пусто при наличии обложки. */
    [[nodiscard]] QString text() const;
    /// Текущая отрисованная обложка (null — заглушка).
    [[nodiscard]] QPixmap pixmap() const { return currentPixmap_; }

    /** @brief Original raw cover data (maximum available resolution).
     *  Empty if no cover loaded. Used by CoverViewer for full-screen display. */
    const QByteArray &coverData() const { return lastCoverData_; }

signals:
    /// Emitted on left-click when a cover is displayed.
    void clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    QSize sizeHint() const override;

private:
    MetadataManager *mgr_ = nullptr;
    int coverPx_ = 128;   ///< сторона квадрата отрисовки, px
    QPixmap currentPixmap_;      ///< текущая обложка (null, если нет)
    QByteArray lastCoverData_;   ///< оригинальные данные для полноэкранного просмотра
};

