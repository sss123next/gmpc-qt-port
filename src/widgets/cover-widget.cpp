#include "cover-widget.h"
#include <QPixmap>

CoverWidget::CoverWidget(QWidget *parent)
    : QLabel(parent), dl_(new AsyncDownload(this))
{
    setMinimumSize(128, 128);
    setAlignment(Qt::AlignCenter);
    setText(tr("No cover"));
    setStyleSheet("border: 1px solid gray; background: #222; color: #888;");
}

void CoverWidget::fetchCover(const QString &artist, const QString &album)
{
    Q_UNUSED(artist);
    Q_UNUSED(album);
    // TODO: LastFM / Discogs cover fetch
}

void CoverWidget::setCover(const QPixmap &pixmap)
{
    setPixmap(pixmap.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void CoverWidget::clear()
{
    setText(tr("No cover"));
    setPixmap(QPixmap());
}
