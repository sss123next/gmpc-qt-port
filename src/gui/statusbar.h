#pragma once

/**
 * @file statusbar.h
 * @brief Bottom status bar.
 *
 * Shows the MPD connection state (connected/disconnected), playback
 * info, and transient application messages.
 */

#include <QWidget>
#include <QLabel>
#include "core/connection.h"

/// Thin strip at the bottom of MainWindow.
class StatusBar : public QWidget {
    Q_OBJECT
public:
    explicit StatusBar(QWidget *parent = nullptr);

    /// Refresh displayed info from a fresh MPD status snapshot.
    void updateStatus(const Connection::Status &s);
    /// Set the connection indicator to "connected" (green).
    void showConnected();
    /// Set the connection indicator to "disconnected" (red).
    void showDisconnected();
    /// Show a transient message for the given duration.
    void showMessage(const QString &msg, int timeoutMs = 5000);

private:
    QLabel *connLabel_;   ///< Connection-state indicator.
    QLabel *infoLabel_;   ///< Playback info / messages.
};

