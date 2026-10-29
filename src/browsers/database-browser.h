#ifndef GMPCQT_DATABASE_BROWSER_H
#define GMPCQT_DATABASE_BROWSER_H

/**
 * @file database-browser.h
 * @brief MPD music database browser (tag-based navigation).
 *
 * Shows the MPD media library organised by artist/album in a QTreeView.
 */

#include "browser-widget.h"

class Connection;
class MusicDatabaseModel;
class QTreeView;

/// Browse the MPD music database by tag.
class DatabaseBrowser : public BrowserWidget {
    Q_OBJECT
public:
    explicit DatabaseBrowser(Connection *conn, QWidget *parent = nullptr);
    ~DatabaseBrowser() override;

    QString title() const override { return tr("Database"); }
    QString iconName() const override { return QStringLiteral("gmpc-database"); }

    void onActivate() override;
    void onConnectionChanged(bool connected) override;

private:
    Connection *conn_;
    MusicDatabaseModel *model_;
    QTreeView *view_;
};

#endif // GMPCQT_DATABASE_BROWSER_H
