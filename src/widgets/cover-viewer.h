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
#include <QLabel>
#include <QKeyEvent>

/// Transient full-screen cover display.
class CoverViewer : public QWidget {
    Q_OBJECT
public:
    explicit CoverViewer(QWidget *parent = nullptr);

    /// Display the given pixmap full-screen.
    void show(const QPixmap &pixmap);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QLabel *label_ = nullptr;
};
