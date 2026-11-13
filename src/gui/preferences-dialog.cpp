#include "preferences-dialog.h"
#include "core/config.h"
#include "core/mpd-commands.h"

#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QFontComboBox>
#include <QColorDialog>
#include <QToolButton>
#include <algorithm>

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

    // ── Proxy group ─────────────────────────────────────────────────
    auto *proxyGroup = new QGroupBox(tr("Proxy"), this);
    auto *proxyForm = new QFormLayout(proxyGroup);

    proxyEnabled_ = new QCheckBox(tr("Enable HTTP proxy"), this);
    proxyForm->addRow(proxyEnabled_);

    proxyGroup_ = new QWidget(this);
    auto *proxySubForm = new QFormLayout(proxyGroup_);
    proxySubForm->setContentsMargins(0, 0, 0, 0);
    proxyHost_ = new QLineEdit(this);
    proxyHost_->setPlaceholderText(tr("proxy.example.com"));
    proxyPort_ = new QSpinBox(this);
    proxyPort_->setRange(1, 65535);
    proxyPort_->setValue(8080);
    proxySubForm->addRow(tr("Host:"), proxyHost_);
    proxySubForm->addRow(tr("Port:"), proxyPort_);
    proxyForm->addRow(proxyGroup_);

    // Toggle proxy detail visibility
    connect(proxyEnabled_, &QCheckBox::toggled, proxyGroup_, &QWidget::setVisible);
    proxyGroup_->setVisible(false);  // hidden until enabled

    root->addWidget(proxyGroup);

    // ── Server group ────────────────────────────────────────────────
    auto *serverGroup = new QGroupBox(tr("Server"), this);
    auto *serverForm = new QFormLayout(serverGroup);

    musicDirectory_ = new QLineEdit(this);
    musicDirectory_->setPlaceholderText(tr("/path/to/music (optional)"));
    serverForm->addRow(tr("Music directory:"), musicDirectory_);

    reconnectDelay_ = new QSpinBox(this);
    reconnectDelay_->setRange(500, 30000);
    reconnectDelay_->setSuffix(tr(" ms"));
    reconnectDelay_->setSingleStep(500);
    serverForm->addRow(tr("Reconnect delay:"), reconnectDelay_);

    connectionTimeout_ = new QSpinBox(this);
    connectionTimeout_->setRange(1000, 60000);
    connectionTimeout_->setSuffix(tr(" ms"));
    connectionTimeout_->setSingleStep(1000);
    serverForm->addRow(tr("Connection timeout:"), connectionTimeout_);

    root->addWidget(serverGroup);

    // ── Fading & Outputs (серверные настройки) ───────────────────────
    // Оригинал держал это страницей серверных настроек (`glade/preferences-server.ui`:
    // cb_fading + sb_fade_time, replay_gain_combo и дерево выходов с тумблерами).
    // Значения живут на MPD: правка уходит командой сразу, а не копится до «Ok».
    liveServerGroup_ = new QGroupBox(tr("Fading & Outputs (server)"), this);
    liveServerGroup_->setObjectName(QStringLiteral("liveServerGroup"));
    auto *liveForm = new QFormLayout(liveServerGroup_);

    fadeHint_ = new QLabel(this);
    fadeHint_->setObjectName(QStringLiteral("fadeHint"));
    liveForm->addRow(fadeHint_);

    crossfadeEnable_ = new QCheckBox(tr("Cross-fading"), this);
    crossfadeEnable_->setObjectName(QStringLiteral("crossfadeEnable"));
    liveForm->addRow(crossfadeEnable_);

    crossfadeSec_ = new QSpinBox(this);
    crossfadeEnable_->setToolTip(tr("Fade between tracks (server-side)"));
    crossfadeSec_->setObjectName(QStringLiteral("crossfadeSec"));
    crossfadeSec_->setRange(1, 60);
    crossfadeSec_->setSuffix(tr(" s"));
    liveForm->addRow(tr("Fade time:"), crossfadeSec_);

    replayGainCombo_ = new QComboBox(this);
    replayGainCombo_->setObjectName(QStringLiteral("replayGainCombo"));
    // Порядок обязан совпадать с enum mpd_replay_gain_mode: индекс — значение.
    replayGainCombo_->addItems({tr("Off"), tr("Track"), tr("Album"), tr("Auto")});
    liveForm->addRow(tr("Replay Gain:"), replayGainCombo_);

    outputList_ = new QListWidget(this);
    outputList_->setObjectName(QStringLiteral("outputList"));
    // Флажок строки — состояние выхода; правка уходит на сервер сразу.
    connect(outputList_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (cmds_ == nullptr || item == nullptr)
            return;
        cmds_->setOutputEnabled(item->data(Qt::UserRole).toInt(),
                                item->checkState() == Qt::Checked);
    });
    liveForm->addRow(tr("Outputs:"), outputList_);

    connect(crossfadeEnable_, &QCheckBox::toggled, this, [this](bool on) {
        crossfadeSec_->setEnabled(on && cmds_ != nullptr);
        if (cmds_ == nullptr || !liveServerGroup_->isEnabled())
            return;
        // Выключение — это `crossfade 0`, а не «забыть про значение»: спин сохраняет
        // последнюю цифру, чтобы включить фейд обратно было чем.
        cmds_->setCrossfade(on ? crossfadeSec_->value() : 0);
    });
    connect(crossfadeSec_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int seconds) {
        if (cmds_ == nullptr || !crossfadeEnable_->isChecked())
            return;   // правка выключенного спина фейд не включает
        cmds_->setCrossfade(seconds);
    });
    connect(replayGainCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (cmds_ == nullptr || !liveServerGroup_->isEnabled())
            return;
        cmds_->setReplayGainMode(index);
    });

    root->addWidget(liveServerGroup_);
    // До bindServer() о сервере ничего не известно: группа выключена и говорит об этом.
    refreshServerSettings();

    // ── Tray group ───────────────────────────────────────────────────
    auto *trayGroup = new QGroupBox(tr("Tray Icon"), this);
    auto *trayForm = new QFormLayout(trayGroup);
    trayEnabled_ = new QCheckBox(tr("Enable system tray icon"), this);
    trayForm->addRow(trayEnabled_);
    root->addWidget(trayGroup);

    // ── Playlist group ──────────────────────────────────────────────
    auto *playlistGroup = new QGroupBox(tr("Playlist"), this);
    auto *playlistForm = new QFormLayout(playlistGroup);

    headerMarkup_ = new QLineEdit(this);
    headerMarkup_->setPlaceholderText(tr("e.g. $artist — $album ($year)"));
    playlistForm->addRow(tr("Header markup:"), headerMarkup_);

    auto *hlRow = new QHBoxLayout();
    highlightColorBtn_ = new QPushButton(this);
    highlightColorBtn_->setFixedSize(32, 24);
    highlightColorBtn_->setToolTip(tr("Click to choose highlight colour"));
    connect(highlightColorBtn_, &QPushButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(
            QColor(highlightColorBtn_->property("color").toString()), this);
        if (c.isValid()) {
            highlightColorBtn_->setStyleSheet(
                QStringLiteral("background: %1").arg(c.name()));
            highlightColorBtn_->setProperty("color", c.name());
        }
    });
    auto *hlReset = new QToolButton(this);
    hlReset->setText(tr("×"));
    hlReset->setToolTip(tr("Reset to default"));
    connect(hlReset, &QToolButton::clicked, this, [this] {
        highlightColorBtn_->setStyleSheet(QString());
        highlightColorBtn_->setProperty("color", QVariant());
    });
    hlRow->addWidget(highlightColorBtn_);
    hlRow->addWidget(hlReset);
    hlRow->addStretch();
    playlistForm->addRow(tr("Highlight colour:"), hlRow);

    root->addWidget(playlistGroup);

    // ── Metadata group ──────────────────────────────────────────────
    auto *metaGroup = new QGroupBox(tr("Metadata"), this);
    auto *metaForm = new QFormLayout(metaGroup);

    metadataFontCombo_ = new QFontComboBox(this);
    metaForm->addRow(tr("Font:"), metadataFontCombo_);

    auto *metaColorRow = new QHBoxLayout();
    metadataColorBtn_ = new QPushButton(this);
    metadataColorBtn_->setFixedSize(32, 24);
    metadataColorBtn_->setToolTip(tr("Click to choose colour"));
    connect(metadataColorBtn_, &QPushButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(
            QColor(metadataColorBtn_->property("color").toString()), this);
        if (c.isValid()) {
            metadataColorBtn_->setStyleSheet(
                QStringLiteral("background: %1").arg(c.name()));
            metadataColorBtn_->setProperty("color", c.name());
        }
    });
    auto *metaColorReset = new QToolButton(this);
    metaColorReset->setText(tr("×"));
    metaColorReset->setToolTip(tr("Reset to default"));
    connect(metaColorReset, &QToolButton::clicked, this, [this] {
        metadataColorBtn_->setStyleSheet(QString());
        metadataColorBtn_->setProperty("color", QVariant());
    });
    metaColorRow->addWidget(metadataColorBtn_);
    metaColorRow->addWidget(metaColorReset);
    metaColorRow->addStretch();
    metaForm->addRow(tr("Colour:"), metaColorRow);

    discogsTokenEdit_ = new QLineEdit(this);
    discogsTokenEdit_->setPlaceholderText(tr("Discogs Personal Access Token"));
    metaForm->addRow(tr("Discogs token:"), discogsTokenEdit_);

    root->addWidget(metaGroup);

    // ── Multimedia keys group ───────────────────────────────────────
    auto *mmGroup = new QGroupBox(tr("Multimedia Keys"), this);
    auto *mmForm = new QFormLayout(mmGroup);

    // Build a static table of known XF86Audio keysyms
    static const struct { quint32 ks; const char *name; } knownKeys[] = {
        { 0x1008ff11, "XF86AudioPlay"       },
        { 0x1008ff12, "XF86AudioPlayPause"  },
        { 0x1008ff13, "XF86AudioStop"       },
        { 0x1008ff14, "XF86AudioPrev"       },
        { 0x1008ff15, "XF86AudioPause"      },
        { 0x1008ff16, "XF86AudioRewind"     },
        { 0x1008ff17, "XF86AudioNext"       },
        { 0x1008ff19, "XF86AudioForward"    },
    };

    for (const auto &k : knownKeys) {
        auto *lbl = new QLabel(QLatin1StringView(k.name), this);
        auto *combo = new QComboBox(this);
        // Populate actions
        using MKA = MediaKeyAction;
        combo->addItem(tr("None"),          static_cast<int>(MKA::None));
        combo->addItem(tr("Play"),          static_cast<int>(MKA::Play));
        combo->addItem(tr("Pause"),         static_cast<int>(MKA::Pause));
        combo->addItem(tr("Play/Pause"),    static_cast<int>(MKA::PlayPause));
        combo->addItem(tr("Stop"),          static_cast<int>(MKA::Stop));
        combo->addItem(tr("Next"),          static_cast<int>(MKA::Next));
        combo->addItem(tr("Previous"),      static_cast<int>(MKA::Previous));
        combo->addItem(tr("Rewind"),        static_cast<int>(MKA::Rewind));
        combo->addItem(tr("Forward"),       static_cast<int>(MKA::Forward));

        mmForm->addRow(lbl, combo);

        KeyBindingRow r;
        r.keysymLabel = lbl;
        r.actionCombo = combo;
        r.binding.keysym = k.ks;
        r.binding.action = MKA::None;
        keyBindingsRows_.append(r);
    }

    root->addWidget(mmGroup);

    // ── Profiles group ──────────────────────────────────────────────
    auto *profGroup = new QGroupBox(tr("Connection Profiles"), this);
    auto *profLayout = new QVBoxLayout(profGroup);

    profileList_ = new QListWidget(this);
    profileList_->setAlternatingRowColors(true);
    profLayout->addWidget(profileList_);

    auto *profBtnRow = new QHBoxLayout();
    addProfileBtn_    = new QPushButton(tr("Add"), this);
    removeProfileBtn_ = new QPushButton(tr("Remove"), this);
    profBtnRow->addWidget(addProfileBtn_);
    profBtnRow->addWidget(removeProfileBtn_);
    profBtnRow->addStretch();
    profLayout->addLayout(profBtnRow);

    connect(addProfileBtn_, &QPushButton::clicked, this, [this] {
        // Check if there's already an unnamed one
        for (int i = 0; i < profiles_.size(); ++i) {
            if (profiles_[i].name.isEmpty()) {
                profileList_->setCurrentRow(i);
                return;
            }
        }
        ConnectionProfile p;
        p.name = QString();   // unnamed — will be set later
        profiles_.append(p);
        profileList_->addItem(tr("(new profile)"));
        profileList_->setCurrentRow(profileList_->count() - 1);
    });

    connect(removeProfileBtn_, &QPushButton::clicked, this, [this] {
        int row = profileList_->currentRow();
        if (row < 0 || row >= profiles_.size())
            return;
        profiles_.removeAt(row);
        delete profileList_->takeItem(row);
    });

    connect(profileList_, &QListWidget::currentItemChanged,
            this, [this](QListWidgetItem *cur, QListWidgetItem *) {
        if (!cur)
            return;
        int row = profileList_->currentRow();
        if (row < 0 || row >= profiles_.size())
            return;
        auto &p = profiles_[row];
        // Inline rename on double-click is too complex for now.
        // For now just allow editing via the connection fields below
        // by copying profile fields into the Connection group.
    });

    root->addWidget(profGroup);

    // ── Buttons ──────────────────────────────────────────────────────
    buttons_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons_);
}

void PreferencesDialog::bindServer(MpdCommands *cmds)
{
    cmds_ = cmds;
    refreshServerSettings();
}

void PreferencesDialog::refreshServerSettings()
{
    // Один запрос вместо двух: crossfade < 0 означает «сервер молчит» (нет соединения
    // или отказ статуса), и тогда остальные поля тоже читать не у кого.
    const int fade = cmds_ == nullptr ? -1 : cmds_->crossfadeSeconds();

    if (fade < 0) {
        liveServerGroup_->setEnabled(false);
        crossfadeSec_->setEnabled(false);
        outputList_->clear();
        replayGainCombo_->setCurrentIndex(0);
        fadeHint_->setText(tr("Not connected"));
        return;
    }

    liveServerGroup_->setEnabled(true);
    fadeHint_->clear();

    {
        // Заливка — не правка пользователя: без блокировки каждое перечитывание
        // слало бы серверу ту же самую команду.
        const QSignalBlocker blockEnable(crossfadeEnable_);
        const QSignalBlocker blockSec(crossfadeSec_);
        crossfadeEnable_->setChecked(fade > 0);
        if (fade > 0)
            crossfadeSec_->setValue(qBound(1, fade, 60));
    }
    // Спин активен только при включённом фейде: правка выключенного спина ничего не шлёт.
    crossfadeSec_->setEnabled(fade > 0);

    const int gain = cmds_->replayGainMode();
    {
        const QSignalBlocker block(replayGainCombo_);
        replayGainCombo_->setCurrentIndex(gain < 0 ? 0 : qBound(0, gain, 3));
    }

    // Выходы: id в роли данных строки — имена дублируются.
    const QSignalBlocker blockList(outputList_);
    outputList_->clear();
    for (const MpdOutput &o : cmds_->outputs()) {
        auto *item = new QListWidgetItem(o.name.isEmpty() ? tr("(unnamed)") : o.name, outputList_);
        item->setData(Qt::UserRole, o.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(o.enabled ? Qt::Checked : Qt::Unchecked);
    }
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
        port_->setValue(cfg.port() > 0 ? cfg.port() : 6600);
    }

    password_->setText(cfg.password());
    autoConnect_->setChecked(cfg.autoConnect());
    stopOnExit_->setChecked(cfg.stopOnExit());
    startHidden_->setChecked(cfg.startHidden());

    // Proxy
    proxyEnabled_->setChecked(cfg.proxyEnabled());
    proxyHost_->setText(cfg.proxyHost());
    proxyPort_->setValue(cfg.proxyPort());
    proxyGroup_->setVisible(cfg.proxyEnabled());

    // Server
    musicDirectory_->setText(cfg.musicDirectory());
    reconnectDelay_->setValue(cfg.reconnectDelay());
    connectionTimeout_->setValue(cfg.connectionTimeout() > 0
                                 ? cfg.connectionTimeout() : 5000);

    trayEnabled_->setChecked(cfg.trayEnabled());

    // Playlist
    headerMarkup_->setText(cfg.playlistHeaderMarkup());
    const QString hlColor = cfg.playlistHighlightColor();
    if (!hlColor.isEmpty()) {
        highlightColorBtn_->setStyleSheet(
            QStringLiteral("background: %1").arg(hlColor));
        highlightColorBtn_->setProperty("color", QColor(hlColor));
    }

    // Metadata
    const QString mFont = cfg.metadataFont();
    if (!mFont.isEmpty())
        metadataFontCombo_->setCurrentFont(QFont(mFont));
    const QString mColor = cfg.metadataColor();
    if (!mColor.isEmpty()) {
        metadataColorBtn_->setStyleSheet(
            QStringLiteral("background: %1").arg(mColor));
        metadataColorBtn_->setProperty("color", QColor(mColor));
    }

    // Metadata — additional
    discogsTokenEdit_->setText(cfg.discogsToken());

    // Profiles
    profiles_ = cfg.profiles();
    profileList_->clear();
    for (const auto &p : profiles_) {
        profileList_->addItem(p.name.isEmpty() ? tr("(unnamed)") : p.name);
    }

    // Multimedia keys — load saved bindings
    keyBindings_ = cfg.mediaKeyBindings();
    for (auto &row : keyBindingsRows_) {
        // Find saved binding for this keysym
        auto it = std::find_if(keyBindings_.begin(), keyBindings_.end(),
            [ks = row.binding.keysym](const MediaKeyBinding &b) {
                return b.keysym == ks;
            });
        int actionVal = 0; // None
        if (it != keyBindings_.end())
            actionVal = static_cast<int>(it->action);
        int idx = row.actionCombo->findData(actionVal);
        if (idx >= 0)
            row.actionCombo->setCurrentIndex(idx);
        else
            row.actionCombo->setCurrentIndex(0);  // fallback to None
    }
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

    // Proxy
    cfg.setProxyEnabled(proxyEnabled_->isChecked());
    cfg.setProxyHost(proxyHost_->text());
    cfg.setProxyPort(proxyPort_->value());

    // Server
    cfg.setMusicDirectory(musicDirectory_->text());
    cfg.setReconnectDelay(reconnectDelay_->value());
    cfg.setConnectionTimeout(connectionTimeout_->value());

    cfg.setTrayEnabled(trayEnabled_->isChecked());

    // Playlist
    cfg.setPlaylistHeaderMarkup(headerMarkup_->text());
    {
        QVariant v = highlightColorBtn_->property("color");
        cfg.setPlaylistHighlightColor(v.isValid() ? v.toString() : QString());
    }

    // Metadata
    {
        QVariant v = metadataColorBtn_->property("color");
        cfg.setMetadataColor(v.isValid() ? v.toString() : QString());
        cfg.setMetadataFont(metadataFontCombo_->currentFont().family());
        cfg.setDiscogsToken(discogsTokenEdit_->text());
    }

    // Profiles — sync names back from list widget
    for (int i = 0; i < profiles_.size(); ++i) {
        QListWidgetItem *item = profileList_->item(i);
        if (item && profiles_[i].name.isEmpty())
            profiles_[i].name = item->text();
    }
    cfg.setProfiles(profiles_);

    // Multimedia keys — collect from combo boxes
    {
        QList<MediaKeyBinding> bindings;
        for (const auto &row : keyBindingsRows_) {
            MediaKeyBinding b;
            b.keysym = row.binding.keysym;
            b.action = static_cast<MediaKeyAction>(
                row.actionCombo->currentData().toInt());
            bindings.append(b);
        }
        cfg.setMediaKeyBindings(bindings);
    }

    cfg.sync();

    QDialog::accept();
}
