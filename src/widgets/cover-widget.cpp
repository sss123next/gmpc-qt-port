#include "cover-widget.h"
#include "metadata/metadata-manager.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>

CoverWidget::CoverWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(128, 128);
    setMouseTracking(false);
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
                    lastCoverData_ = data;
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

void CoverWidget::setCoverSize(int px)
{
    if (px <= 0 || px == coverPx_)
        return;
    coverPx_ = px;
    setMinimumSize(coverPx_, coverPx_);
}

void CoverWidget::fetchCover(const QString &artist, const QString &album,
                             const QString &uri)
{
    // Сразу сбрасываем старую обложку, чтобы не висела до ответа сети/кеша.
    clear();

    if (mgr_)
        mgr_->requestCover(artist, album, uri);
}

void CoverWidget::setCover(const QPixmap &pixmap)
{
    currentPixmap_ = pixmap.scaled(coverPx_, coverPx_,
                                   Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation);
    update();
}

void CoverWidget::clear()
{
    currentPixmap_ = QPixmap();
    lastCoverData_.clear();
    update();
}

QString CoverWidget::text() const
{
    // Заглушка рисуется тем же текстом, что отдаётся наружу: тест не должен
    // знать про paintEvent.
    return currentPixmap_.isNull() ? tr("No cover") : QString();
}

void CoverWidget::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const QRect r = rect();

    // ── Fill background ───────────────────────────────────────────────
    p.fillRect(r, QColor(0x22, 0x22, 0x22));

    if (!currentPixmap_.isNull()) {
        // ── Draw the cover, centered ──────────────────────────────────
        const QPixmap &scaled = currentPixmap_;
        const int x = r.x() + (r.width()  - scaled.width())  / 2;
        const int y = r.y() + (r.height() - scaled.height()) / 2;
        p.drawPixmap(x, y, scaled);
    } else {
        // ── Draw "No cover" placeholder ───────────────────────────────
        p.setPen(QColor(0x88, 0x88, 0x88));
        QFont f = p.font();
        f.setPointSize(qMax(8, coverPx_ / 12));
        p.setFont(f);
        p.drawText(r, Qt::AlignCenter, tr("No cover"));
    }

    // ── Border ────────────────────────────────────────────────────────
    p.setPen(QColor(0x55, 0x55, 0x55));
    p.drawRect(r.adjusted(0, 0, -1, -1));
}

QSize CoverWidget::sizeHint() const
{
    return QSize(coverPx_, coverPx_);
}

void CoverWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !currentPixmap_.isNull()) {
        emit clicked();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}
