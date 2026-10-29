#pragma once

/**
 * @file file-browser.h
 * @brief MPD file-system browser.
 *
 * Navigates the MPD server's music directory as a filesystem tree.
 * TODO — full implementation pending.
 */

#include "browser-widget.h"

class Connection;

/// Browse MPD's virtual filesystem.
class FileBrowser : public BrowserWidget {
    Q_OBJECT
public:
    explicit FileBrowser(Connection *conn, QWidget *parent = nullptr);
    QString title() const override { return tr("Files"); }
    QString iconName() const override { return QStringLiteral("folder"); }
};

