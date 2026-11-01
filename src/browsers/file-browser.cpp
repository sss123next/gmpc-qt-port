/**
 * @file file-browser.cpp
 * @brief MPD filesystem browser — 2-pane: directory tree + song list.
 */

#include "file-browser.h"
#include "file-system-model.h"
#include "core/connection.h"
#include <QHeaderView>
#include <QSplitter>
#include <QTreeView>
#include <QVBoxLayout>

// ── FileContentProxy ────────────────────────────────────────────────────────

FileContentProxy::FileContentProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void FileContentProxy::setRootPath(const QString &path)
{
    beginResetModel();
    rootPath_ = path;
    endResetModel();
}

bool FileContentProxy::filterAcceptsRow(int sourceRow,
                                        const QModelIndex &sourceParent) const
{
    // If no root path is set, show nothing
    if (rootPath_.isEmpty())
        return false;

    // Accept only if the parent's URI matches our root path
    QModelIndex parentIdx = sourceModel()->parent(
        sourceModel()->index(sourceRow, 0, sourceParent));
    if (!parentIdx.isValid())
        return rootPath_.isEmpty(); // root item — accept only if root is selected

    const QString parentPath = sourceModel()->data(
        parentIdx, FileSystemModel::UriRole).toString();
    return parentPath == rootPath_;
}

// ── FileBrowser ─────────────────────────────────────────────────────────────

FileBrowser::FileBrowser(Connection *conn, QWidget *parent)
    : BrowserWidget(parent)
    , conn_(conn)
    , model_(new FileSystemModel(conn, this))
    , contentProxy_(new FileContentProxy(this))
    , leftView_(new QTreeView(this))
    , rightView_(new QTreeView(this))
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    auto *splitter = new QSplitter(Qt::Horizontal, this);

    // Left pane: directory tree
    leftView_->setModel(model_);
    leftView_->setHeaderHidden(true);
    leftView_->setExpandsOnDoubleClick(true);
    leftView_->setSelectionMode(QAbstractItemView::SingleSelection);
    leftView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    leftView_->header()->setStretchLastSection(true);

    // Right pane: file list
    contentProxy_->setSourceModel(model_);
    rightView_->setModel(contentProxy_);
    rightView_->setRootIsDecorated(false);
    rightView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    rightView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    rightView_->setSortingEnabled(true);
    rightView_->header()->setStretchLastSection(true);
    rightView_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

    splitter->addWidget(leftView_);
    splitter->addWidget(rightView_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({250, 400});

    lay->addWidget(splitter);

    connect(leftView_->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this, &FileBrowser::onLeftSelectionChanged);
}

FileBrowser::~FileBrowser() = default;

void FileBrowser::onLeftSelectionChanged()
{
    QModelIndex idx = leftView_->currentIndex();
    if (!idx.isValid()) {
        contentProxy_->setRootPath({});
        return;
    }

    // Get the path from the model (UriRole)
    const QString path = idx.data(FileSystemModel::UriRole).toString();
    contentProxy_->setRootPath(path);

    // Auto-expand the right view columns
    for (int c = 0; c < rightView_->header()->count(); ++c)
        rightView_->resizeColumnToContents(c);
}

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
