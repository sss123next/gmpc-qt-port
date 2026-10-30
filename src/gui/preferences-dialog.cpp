#include "preferences-dialog.h"
#include "core/config.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QStackedWidget>

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    setupUi();
    loadFromConfig();
}

void PreferencesDialog::setupUi()
{
    auto *root = new QVBoxLayout(this);

    // ── Connection group ─────────────────────────────────────────────
    auto *connGroup = new QGroupBox(tr("Connection"), this);
    auto *connForm = new QFormLayout(connGroup);

    connectionMode_ = new QComboBox(this);
    connectionMode_->addItem(tr("TCP (host:port)"));
    connectionMode_->addItem(tr("Unix socket"));
    connForm->addRow(tr("Mode:"), connectionMode_);

    // TCP sub-group (shown/hidden)
    tcpGroup_ = new QWidget(this);
    auto *tcpForm = new QFormLayout(tcpGroup_);
    tcpForm->setContentsMargins(0, 0, 0, 0);
    host_ = new QLineEdit(this);
    host_->setObjectName("prefHost");
    port_ = new QSpinBox(this);
    port_->setRange(1, 65535);
    port_->setValue(6600);
    tcpForm->addRow(tr("Host:"), host_);
    tcpForm->addRow(tr("Port:"), port_);

    // Socket path
    socketPath_ = new QLineEdit(this);
    socketPath_->setPlaceholderText(tr("/path/to/mpd.sock"));

    password_ = new QLineEdit(this);
    password_->setEchoMode(QLineEdit::Password);

    autoConnect_ = new QCheckBox(tr("Connect automatically at startup"), this);
    autoConnect_->setChecked(true);

    // Stack mode switcher: index 0 = TCP widget, index 1 = socket line
    auto *connStack = new QStackedWidget(this);
    connStack->addWidget(tcpGroup_);
    connStack->addWidget(socketPath_);
    connForm->addRow(tr("Address:"), connStack);

    connForm->addRow(tr("Password:"), password_);
    connForm->addRow(autoConnect_);

    connect(connectionMode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            connStack, &QStackedWidget::setCurrentIndex);

    root->addWidget(connGroup);

    // ── Playback group ───────────────────────────────────────────────
    auto *playGroup = new QGroupBox(tr("Playback"), this);
    auto *playForm = new QFormLayout(playGroup);
    stopOnExit_ = new QCheckBox(tr("Stop playback on exit"), this);
    playForm->addRow(stopOnExit_);
    root->addWidget(playGroup);

    // ── Window group ─────────────────────────────────────────────────
    auto *winGroup = new QGroupBox(tr("Window"), this);
    auto *winForm = new QFormLayout(winGroup);
    startHidden_ = new QCheckBox(tr("Start minimized to tray"), this);
    winForm->addRow(startHidden_);
    root->addWidget(winGroup);

    // ── Tray group ───────────────────────────────────────────────────
    auto *trayGroup = new QGroupBox(tr("Tray Icon"), this);
    auto *trayForm = new QFormLayout(trayGroup);
    trayEnabled_ = new QCheckBox(tr("Enable system tray icon"), this);
    trayForm->addRow(trayEnabled_);
    root->addWidget(trayGroup);

    // ── Buttons ──────────────────────────────────────────────────────
    buttons_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons_);
}

void PreferencesDialog::loadFromConfig()
{
    auto &cfg = Config::instance();

    const QString sock = cfg.socketPath();
    if (!sock.isEmpty()) {
        connectionMode_->setCurrentIndex(1);  // Unix socket
        socketPath_->setText(sock);
    } else {
        connectionMode_->setCurrentIndex(0);  // TCP
        host_->setText(cfg.host());
        port_->setValue(cfg.port());
    }

    password_->setText(cfg.password());
    autoConnect_->setChecked(cfg.autoConnect());
    stopOnExit_->setChecked(cfg.stopOnExit());
    startHidden_->setChecked(cfg.startHidden());
    trayEnabled_->setChecked(cfg.trayEnabled());
}

void PreferencesDialog::accept()
{
    auto &cfg = Config::instance();

    if (connectionMode_->currentIndex() == 1) {
        // Unix socket mode — save socket path, clear host:port
        cfg.setSocketPath(socketPath_->text());
        cfg.setHost(QString());   // clear TCP fields
        cfg.setPort(0);
    } else {
        // TCP mode — save host:port, clear socket path
        cfg.setHost(host_->text());
        cfg.setPort(port_->value());
        cfg.setSocketPath(QString());
    }

    cfg.setPassword(password_->text());
    cfg.setAutoConnect(autoConnect_->isChecked());
    cfg.setStopOnExit(stopOnExit_->isChecked());
    cfg.setStartHidden(startHidden_->isChecked());
    cfg.setTrayEnabled(trayEnabled_->isChecked());
    cfg.sync();

    QDialog::accept();
}
