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
    , proxy_(new QSortFilterProxyModel(this))
    , filterEdit_(new QLineEdit(this))
{
    proxy_->setSourceModel(model_);
    proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy_->setFilterKeyColumn(-1); // search all columns

    setupUi();

    connect(filterEdit_, &QLineEdit::textChanged,
            this, &CurrentPlaylist::onFilterChanged);
}

void CurrentPlaylist::setupUi()
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    filterEdit_->setPlaceholderText(tr("Search playlist…"));
    filterEdit_->setClearButtonEnabled(true);
    lay->addWidget(filterEdit_);

    view_ = new QTreeView(this);
    view_->setModel(proxy_);
    view_->setAlternatingRowColors(true);
    view_->setRootIsDecorated(false);
    view_->setSortingEnabled(true);
    view_->header()->setStretchLastSection(true);

    lay->addWidget(view_, 1);
}

void CurrentPlaylist::onFilterChanged(const QString &text)
{
    proxy_->setFilterFixedString(text);
}
