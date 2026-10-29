/**
 * @file file-browser.cpp
 * @brief MPD filesystem browser — QTreeView + FileSystemModel.
 */

#include "file-browser.h"
#include "file-system-model.h"
#include "core/connection.h"
#include <QVBoxLayout>
#include <QTreeView>

FileBrowser::FileBrowser(Connection *conn, QWidget *parent)
    : BrowserWidget(parent)
    , conn_(conn)
    , model_(new FileSystemModel(conn, this))
    , view_(new QTreeView(this))
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    view_->setModel(model_);
    view_->setHeaderHidden(true);
    view_->setExpandsOnDoubleClick(true);
    lay->addWidget(view_);
    setEnabled(false); // disabled until connected
}

FileBrowser::~FileBrowser() = default;

void FileBrowser::onActivate()
{
    if (conn_->isConnected() && model_->canFetchMore({}))
        model_->fetchMore({});
}

void FileBrowser::onConnectionChanged(bool connected)
{
    setEnabled(connected);
    if (connected) {
        model_->refresh();
        model_->fetchMore({});
    }
}
