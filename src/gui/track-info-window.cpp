/**
 * @file track-info-window.cpp
 * @brief Окно метаданных выбранного трэка.
 */

#include "track-info-window.h"
#include "widgets/track-info-panel.h"

#include <QVBoxLayout>

TrackInfoWindow::TrackInfoWindow(const SongEntry &song,
                                 Connection *conn,
                                 MetadataManager *mgr,
                                 QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Track Info"));
    resize(600, 520);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    panel_ = new TrackInfoPanel(this);
    panel_->setConnection(conn);
    if (mgr)
        panel_->setMetadataManager(mgr);
    panel_->showTrack(song);

    lay->addWidget(panel_);
}
