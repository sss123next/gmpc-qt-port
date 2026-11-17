#pragma once

/**
 * @file cover-viewer.h
 * @brief Full-screen cover viewer — frameless, click/escape to close.
 *
 * Shows a full-resolution album-cover image centred on the screen.
 * Left-click or Escape hides the window (it is reused, not deleted on close).
 */

#include <QWidget>
#include <QPixmap>
#include <QByteArray>
#include <QLabel>
#include <QKeyEvent>
#include <QMouseEvent>

/// Full-screen cover display (reused, not deleted on close).
class CoverViewer : public QWidget {
    Q_OBJECT
public:
    explicit CoverViewer(QWidget *parent = nullptr);

    /// Load @p data (JPEG/PNG raw bytes) and display full-screen.
    void show(const QByteArray &data);

    /// Release the pixmap memory (call on hide / track change).
    void clear();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QLabel *label_ = nullptr;
};
