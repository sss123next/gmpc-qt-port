#ifndef GMPCQT_SEARCH_BROWSER_H
#define GMPCQT_SEARCH_BROWSER_H

/**
 * @file search-browser.h
 * @brief MPD search browser.
 *
 * Provides full-text search against the MPD music database.
 * TODO — full implementation pending.
 */

#include "browser-widget.h"

class Connection;

/// Search the MPD database.
class SearchBrowser : public BrowserWidget {
    Q_OBJECT
public:
    explicit SearchBrowser(Connection *conn, QWidget *parent = nullptr);
    QString title() const override { return tr("Search"); }
    QString iconName() const override { return QStringLiteral("edit-find"); }
};

#endif // GMPCQT_SEARCH_BROWSER_H
