/**
 * @file playlist-editor.cpp
 * @brief MPD stored-playlist editor skeleton.
 */

#include "playlist-editor.h"
#include <QLabel>
#include <QVBoxLayout>

PlaylistEditor::PlaylistEditor(Connection *conn, QWidget *parent)
    : BrowserWidget(parent)
{
    Q_UNUSED(conn);
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(tr("Playlist Editor — TODO"), this));
}
