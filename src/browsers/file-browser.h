#pragma once

/**
 * @file file-browser.h
 * @brief MPD file-system browser (files/directories/playlists).
 *
 * Navigates the MPD server's music directory via filesystem tree.
 * Uses FileSystemModel with lazy loading (canFetchMore/fetchMore).
 */

#include "browser-widget.h"

class Connection;
class FileSystemModel;
class QTreeView;

/// Browse MPD's virtual filesystem.
class FileBrowser : public BrowserWidget {
    Q_OBJECT
public:
    explicit FileBrowser(Connection *conn, QWidget *parent = nullptr);
    ~FileBrowser() override;

    QString title() const override { return tr("Files"); }
    QString iconName() const override { return QStringLiteral("folder"); }

    void onActivate() override;
    void onConnectionChanged(bool connected) override;

private:
    Connection *conn_;
    FileSystemModel *model_;
    QTreeView *view_;
};

