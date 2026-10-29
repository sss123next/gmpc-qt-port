/**
 * @file database-browser.cpp
 * @brief MPD music database browser skeleton.
 */

#include "database-browser.h"
#include <QLabel>
#include <QVBoxLayout>

DatabaseBrowser::DatabaseBrowser(Connection *conn, QWidget *parent)
    : BrowserWidget(parent), conn_(conn)
{
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(tr("Database Browser — TODO"), this));
}
