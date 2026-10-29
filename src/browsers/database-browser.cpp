/**
 * @file database-browser.cpp
 * @brief MPD music database browser with tag-based QTreeView.
 */

#include "database-browser.h"
#include "music-database-model.h"
#include <QVBoxLayout>
#include <QTreeView>

DatabaseBrowser::DatabaseBrowser(Connection *conn, QWidget *parent)
    : BrowserWidget(parent)
    , conn_(conn)
    , model_(new MusicDatabaseModel(conn, this))
    , view_(new QTreeView(this))
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    view_->setModel(model_);
    view_->setHeaderHidden(false);
    view_->setExpandsOnDoubleClick(true);
    lay->addWidget(view_);
    setEnabled(false); // disabled until connected
}

DatabaseBrowser::~DatabaseBrowser() = default;

void DatabaseBrowser::onActivate()
{
    if (conn_->isConnected())
        model_->loadArtists();
}

void DatabaseBrowser::onConnectionChanged(bool connected)
{
    setEnabled(connected);
    if (connected)
        model_->loadArtists();
}
