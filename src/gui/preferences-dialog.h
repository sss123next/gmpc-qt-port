#pragma once

/**
 * @file preferences-dialog.h
 * @brief Application preferences dialog.
 *
 * Provides a QDialog with settings groups for Connection (host, port,
 * socket, password, auto-connect), Playback (stop-on-exit), and
 * Window (start-hidden).  Reads/writes via Config singleton.
 */

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>

/// Modal settings dialog bound to Config.
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

    /// Apply current UI values to Config and sync to disk.
    void accept() override;

private:
    void setupUi();
    void loadFromConfig();

    // Connection group
    QComboBox    *connectionMode_;   ///< "TCP" or "Unix socket"
    QWidget      *tcpGroup_;         ///< holds host_ + port_ widgets
    QLineEdit    *host_;
    QSpinBox     *port_;
    QLineEdit    *socketPath_;
    QLineEdit    *password_;
    QCheckBox    *autoConnect_;

    // Playback group
    QCheckBox    *stopOnExit_;

    // Window group
    QCheckBox    *startHidden_;

    // Tray group
    QCheckBox    *trayEnabled_;

    QDialogButtonBox *buttons_;
};
