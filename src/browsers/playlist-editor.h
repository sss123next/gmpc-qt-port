#pragma once

/**
 * @file playlist-editor.h
 * @brief MPD stored-playlist editor.
 *
 * Lists, creates, renames, and deletes saved playlists.
 * TODO — full implementation pending.
 */

#include "browser-widget.h"

class Connection;

/// Manage MPD stored playlists.
class PlaylistEditor : public BrowserWidget {
    Q_OBJECT
public:
    explicit PlaylistEditor(Connection *conn, QWidget *parent = nullptr);
    QString title() const override { return tr("Playlists"); }
    QString iconName() const override { return QStringLiteral("media-playlist"); }
};

