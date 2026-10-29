#ifndef GMPCQT_DATABASE_BROWSER_H
#define GMPCQT_DATABASE_BROWSER_H

/**
 * @file database-browser.h
 * @brief MPD music database browser (tag-based navigation).
 *
 * Shows the MPD media library organised by artist/album/genre.
 * TODO — full implementation pending.
 */

#include "browser-widget.h"

class Connection;

/// Browse the MPD music database by tag.
class DatabaseBrowser : public BrowserWidget {
    Q_OBJECT
public:
    explicit DatabaseBrowser(Connection *conn, QWidget *parent = nullptr);
    QString title() const override { return tr("Database"); }
    QString iconName() const override { return QStringLiteral("gmpc-database"); }

private:
    Connection *conn_;
};

#endif // GMPCQT_DATABASE_BROWSER_H
