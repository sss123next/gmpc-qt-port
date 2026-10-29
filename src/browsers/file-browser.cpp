/**
 * @file file-browser.cpp
 * @brief MPD filesystem browser skeleton.
 */

#include "file-browser.h"
#include <QLabel>
#include <QVBoxLayout>

FileBrowser::FileBrowser(Connection *conn, QWidget *parent)
    : BrowserWidget(parent)
{
    Q_UNUSED(conn);
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(tr("File Browser — TODO"), this));
}
