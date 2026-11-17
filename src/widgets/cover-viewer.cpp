/**
 * @file cover-viewer.cpp
 * @brief Full-screen cover viewer — frameless, click/escape to close.
 */

#include "cover-viewer.h"
#include <QScreen>
#include <QApplication>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>

CoverViewer::CoverViewer(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setStyleSheet("background: #111;");

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    label_ = new QLabel(this);
    label_->setAlignment(Qt::AlignCenter);
    lay->addWidget(label_);
}

void CoverViewer::show(const QPixmap &pixmap)
{
    if (pixmap.isNull())
        return;

    // Scale to fit the screen, preserving aspect ratio.
    const QScreen *screen = QApplication::primaryScreen();
    if (!screen) { deleteLater(); return; }
    const QSize screenSize = screen->availableSize();

    QPixmap scaled = pixmap.scaled(screenSize, Qt::KeepAspectRatio,
                                    Qt::SmoothTransformation);
    label_->setPixmap(scaled);

    resize(screenSize);
    const QRect geo = screen->availableGeometry();
    move(geo.topLeft());

    QWidget::show();
    raise();
}

void CoverViewer::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

void CoverViewer::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        close();
        return;
    }
    QWidget::mousePressEvent(event);
}
