/**
 * @file current-playlist.cpp
 * @brief Current playlist view implementation.
 */

#include "current-playlist.h"
#include "core/connection.h"
#include "core/playlist-model.h"

#include <QVBoxLayout>
#include <QHeaderView>

CurrentPlaylist::CurrentPlaylist(Connection *conn, PlaylistModel *model,
                                 QWidget *parent)
    : BrowserWidget(parent), conn_(conn), model_(model)
{
    setupUi();
}

void CurrentPlaylist::setupUi()
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    view_ = new QTreeView(this);
    view_->setModel(model_);
    view_->setAlternatingRowColors(true);
    view_->setRootIsDecorated(false);
    view_->setSortingEnabled(true);
    view_->header()->setStretchLastSection(true);

    lay->addWidget(view_);
}
