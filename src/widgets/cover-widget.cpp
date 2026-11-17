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
    // Если данные есть в кеше, coverReady придёт синхронно из requestCover()
    // — мерцание незаметно.  Без этого clear старый артефакт висит, пока
    // каскад провайдеров не доберётся до картинки.
    clear();

    if (mgr_)
        mgr_->requestCover(artist, album, uri);
}

void CoverWidget::setCover(const QPixmap &pixmap)
{
    setPixmap(pixmap.scaled(coverPx_, coverPx_, Qt::KeepAspectRatio,
                            Qt::SmoothTransformation));
}

void CoverWidget::clear()
{
    setPixmap(QPixmap());          // must come before setText (clears text)
    setText(tr("No cover"));
}
