#include "cover-widget.h"
#include "metadata/metadata-manager.h"
#include <QPixmap>

CoverWidget::CoverWidget(QWidget *parent)
    : QLabel(parent)
{
    setMinimumSize(128, 128);
    setAlignment(Qt::AlignCenter);
    setText(tr("No cover"));
    setStyleSheet("border: 1px solid gray; background: #222; color: #888;");
}

void CoverWidget::setMetadataManager(MetadataManager *mgr)
{
    mgr_ = mgr;
    if (mgr_) {
        connect(mgr_, &MetadataManager::coverReady, this,
                [this](const QString &artist, const QString &album,
                       const QByteArray &data) {
                    Q_UNUSED(artist);
                    Q_UNUSED(album);
                    QPixmap pm;
                    if (pm.loadFromData(data))
                        setCover(pm);
                });
        connect(mgr_, &MetadataManager::coverNotFound, this,
                [this](const QString &, const QString &) {
                    clear();
                });
    }
}

void CoverWidget::fetchCover(const QString &artist, const QString &album)
{
    if (mgr_) {
        mgr_->requestCover(artist, album);
    } else {
        clear();
    }
}

void CoverWidget::setCover(const QPixmap &pixmap)
{
    setPixmap(pixmap.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void CoverWidget::clear()
{
    setPixmap(QPixmap());          // must come before setText (clears text)
    setText(tr("No cover"));
}
