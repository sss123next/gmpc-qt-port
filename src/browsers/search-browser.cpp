/**
 * @file search-browser.cpp
 * @brief MPD search browser skeleton.
 */

#include "search-browser.h"
#include <QLabel>
#include <QVBoxLayout>

SearchBrowser::SearchBrowser(Connection *conn, QWidget *parent)
    : BrowserWidget(parent)
{
    Q_UNUSED(conn);
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(tr("Search Browser — TODO"), this));
}
