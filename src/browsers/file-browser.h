#pragma once

/**
 * @file file-browser.h
 * @brief MPD file-system browser (2-pane: tree + song list).
 *
 * Left pane: directory tree. Right pane: songs in selected directory.
 * Uses FileSystemModel with lazy loading (canFetchMore/fetchMore).
 */

#include "browser-widget.h"

#include <QSortFilterProxyModel>
#include <QSet>

class Connection;
class FileSystemModel;
class QTreeView;

/// Proxy that shows only direct children of the selected directory.
class FileContentProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit FileContentProxy(QObject *parent = nullptr);

    void setRootPath(const QString &path);
    QString rootPath() const { return rootPath_; }

protected:
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex &sourceParent) const override;

private:
    QString rootPath_;
};

/// Browse MPD's virtual filesystem.
class FileBrowser : public BrowserWidget {
    Q_OBJECT
public:
    explicit FileBrowser(Connection *conn, QWidget *parent = nullptr);
    ~FileBrowser() override;

    QString title() const override { return tr("Files"); }
    QString iconName() const override { return QStringLiteral("folder"); }

    /// The embedded tree view (for context menu setup).
    QTreeView *treeView() const { return leftView_; }

    void onActivate() override;
    void onConnectionChanged(bool connected) override;

private slots:
    void onLeftSelectionChanged();

private:
    Connection *conn_;
    FileSystemModel *model_;
    FileContentProxy *contentProxy_;
    QTreeView *leftView_;   ///< directory tree
    QTreeView *rightView_;  ///< song list for selected directory
};

