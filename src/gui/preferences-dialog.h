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
#include <QListWidget>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QListView>
#include <QLabel>
#include <QFontComboBox>
#include "core/config.h"

class MpdCommands;

/// Modal settings dialog bound to Config.
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

    /// Apply current UI values to Config and sync to disk.
    void accept() override;

    /** @brief Привязать транспорт сервера: fading, выходы и ReplayGain живут на нём,
     *  а не в Config — их значение читается у MPD при открытии окна.
     *
     *  Без транспорта группа остаётся выключенной с надписью «Not connected»:
     *  показывать нули сервера, о котором ничего не известно, значило бы врать.
     */
    void bindServer(MpdCommands *cmds);

public slots:
    /// Перечитать crossfade/выходы у сервера (при открытии и по idle OUTPUT).
    void refreshServerSettings();

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

    // Proxy group
    QCheckBox    *proxyEnabled_;
    QWidget      *proxyGroup_;   ///< holds host_ + port_ widgets
    QLineEdit    *proxyHost_;
    QSpinBox     *proxyPort_;

    // Server group
    QLineEdit    *musicDirectory_;
    QSpinBox     *reconnectDelay_;
    QSpinBox     *connectionTimeout_;

    // Серверные настройки (живут у MPD, не в Config)
    MpdCommands  *cmds_ = nullptr;   ///< транспорт; без него группа выключена
    QGroupBox    *liveServerGroup_;  ///< «Fading & Outputs» — целиком гасится вне соединения
    QLabel       *fadeHint_;         ///< «Not connected», когда сервер молчит
    QCheckBox    *crossfadeEnable_;
    QSpinBox     *crossfadeSec_;
    QComboBox    *replayGainCombo_;
    QListWidget  *outputList_;

    // Tray group
    QCheckBox    *trayEnabled_;

    // Tray Popup group
    QCheckBox    *trayPopupEnabled_;
    QComboBox    *trayPopupPosition_;
    QSpinBox     *trayPopupTimeout_;
    QComboBox    *trayPopupStyle_;
    QCheckBox    *trayPopupShowCover_;
    QCheckBox    *trayPopupShowButtons_;
    QSpinBox     *trayPopupXOffset_;
    QSpinBox     *trayPopupYOffset_;

    // Profiles group
    QListWidget  *profileList_;
    QPushButton  *addProfileBtn_;
    QPushButton  *removeProfileBtn_;
    QList<ConnectionProfile> profiles_;    ///< working copy until accept()

    // Playlist group
    QLineEdit    *headerMarkup_;
    QPushButton  *highlightColorBtn_;   ///< opens QColorDialog; shows chosen colour

    // Metadata group
    QFontComboBox *metadataFontCombo_;
    QPushButton   *metadataColorBtn_;
    QLineEdit     *discogsTokenEdit_;

    // Multimedia-keys group
    struct KeyBindingRow {
        QLabel     *keysymLabel;   ///< Human-readable keysym name
        QComboBox  *actionCombo;   ///< Action selector
        MediaKeyBinding binding;   ///< Original keysym+action
    };
    QList<KeyBindingRow> keyBindingsRows_;
    QList<MediaKeyBinding> keyBindings_;   ///< working copy until accept()

    QDialogButtonBox *buttons_;
};
