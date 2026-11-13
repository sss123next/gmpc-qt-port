#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

/**
 * @file test-preferences-dialog.cpp
 * @brief Tests for PreferencesDialog.
 *
 * Uses DOCTEST_CONFIG_IMPLEMENT + explicit main() with QApplication on stack
 * to avoid segfault on static QApplication destruction (Qt6 requirement).
 */

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>

#include "gui/preferences-dialog.h"
#include "core/config.h"
#include "core/connection.h"
#include "core/mpd-commands.h"

/// Reset all config keys to their defaults, so tests don't leak across runs.
static void resetConfig()
{
    auto &cfg = Config::instance();
    cfg.setHost(QString());
    cfg.setPort(0);
    cfg.setSocketPath(QString());
    cfg.setPassword(QString());
    cfg.setAutoConnect(true);
    cfg.setStopOnExit(false);
    cfg.setStartHidden(false);
    cfg.sync();
}

TEST_CASE("PreferencesDialog is a QDialog")
{
    resetConfig();
    PreferencesDialog dlg;
    CHECK(dlg.isWindow());
}

TEST_CASE("PreferencesDialog has connection mode combo")
{
    resetConfig();
    PreferencesDialog dlg;
    auto *combo = dlg.findChild<QComboBox *>();
    REQUIRE(combo != nullptr);
    CHECK_EQ(combo->count(), 2);  // TCP, Unix socket
}

TEST_CASE("PreferencesDialog has host and port widgets")
{
    resetConfig();
    PreferencesDialog dlg;
    CHECK(dlg.findChild<QLineEdit *>() != nullptr);     // host
    CHECK(dlg.findChild<QSpinBox *>() != nullptr);      // port
}

TEST_CASE("PreferencesDialog has Ok/Cancel buttons")
{
    resetConfig();
    PreferencesDialog dlg;
    auto *box = dlg.findChild<QDialogButtonBox *>();
    REQUIRE(box != nullptr);
    auto btns = box->standardButtons();
    CHECK((btns & QDialogButtonBox::Ok) != 0);
    CHECK((btns & QDialogButtonBox::Cancel) != 0);
}

TEST_CASE("PreferencesDialog has connection groups")
{
    resetConfig();
    PreferencesDialog dlg;
    auto groups = dlg.findChildren<QGroupBox *>();
    // Connection, Playback, Window
    CHECK_GE(groups.size(), 3);
}

TEST_CASE("PreferencesDialog loads config defaults")
{
    resetConfig();
    PreferencesDialog dlg;
    auto *combo = dlg.findChild<QComboBox *>();
    REQUIRE(combo != nullptr);
    // default config has no socketPath, so TCP mode (index 0)
    CHECK_EQ(combo->currentIndex(), 0);
}

TEST_CASE("PreferencesDialog reads socketPath from config")
{
    resetConfig();
    // Set socket path before creating dialog
    auto &cfg = Config::instance();
    cfg.setSocketPath("/tmp/test-mpd.sock");
    cfg.sync();

    PreferencesDialog dlg;
    auto *combo = dlg.findChild<QComboBox *>();
    REQUIRE(combo != nullptr);
    CHECK_EQ(combo->currentIndex(), 1);  // Unix socket mode
}

TEST_CASE("PreferencesDialog accept saves TCP settings")
{
    resetConfig();
    auto &cfg = Config::instance();

    PreferencesDialog dlg;

    // Modify TCP fields
    auto *host = dlg.findChild<QLineEdit *>("prefHost");
    auto *port = dlg.findChild<QSpinBox *>();
    REQUIRE(host != nullptr);
    REQUIRE(port != nullptr);
    host->setText("mpd.example.com");
    port->setValue(6601);

    QObject::connect(&dlg, &QDialog::accepted, [&]() {
        CHECK_EQ(cfg.host().toStdString(), "mpd.example.com");
        CHECK_EQ(cfg.port(), 6601);
        CHECK(cfg.socketPath().isEmpty());
    });

    dlg.accept();

    // Also verify directly
    CHECK_EQ(cfg.host().toStdString(), "mpd.example.com");
    CHECK_EQ(cfg.port(), 6601);
    CHECK(cfg.socketPath().isEmpty());
}

TEST_CASE("PreferencesDialog accept saves socket path")
{
    resetConfig();
    auto &cfg = Config::instance();

    PreferencesDialog dlg;

    // Switch to socket mode and set path
    auto *combo = dlg.findChild<QComboBox *>();
    REQUIRE(combo != nullptr);
    combo->setCurrentIndex(1);

    auto *socketLine = dlg.findChild<QLineEdit *>("", Qt::FindChildrenRecursively);
    // The first QLineEdit is host_, second is password_, third is socketPath_.
    // Better: find the socket path by placeholder
    auto lines = dlg.findChildren<QLineEdit *>();
    QLineEdit *sockLine = nullptr;
    for (auto *l : lines) {
        if (l->placeholderText().contains("sock", Qt::CaseInsensitive)) {
            sockLine = l;
            break;
        }
    }
    REQUIRE(sockLine != nullptr);
    sockLine->setText("/run/mpd/socket");

    dlg.accept();

    CHECK_EQ(cfg.socketPath().toStdString(), "/run/mpd/socket");
    CHECK(cfg.host().isEmpty());  // TCP cleared
    CHECK_EQ(cfg.port(), 0);       // TCP cleared
}

// ── Multimedia keys tests ───────────────────────────────────────────────────

TEST_CASE("PreferencesDialog has multimedia keys group")
{
    resetConfig();
    PreferencesDialog dlg;
    // Find "Multimedia Keys" group box
    auto groups = dlg.findChildren<QGroupBox *>();
    auto it = std::find_if(groups.begin(), groups.end(),
        [](QGroupBox *g) { return g->title() == "Multimedia Keys"; });
    CHECK(it != groups.end());
}

TEST_CASE("PreferencesDialog multimedia keys has 8 row combos")
{
    resetConfig();
    PreferencesDialog dlg;

    // Find all QComboBox inside the Multimedia Keys group
    auto groups = dlg.findChildren<QGroupBox *>();
    QGroupBox *mmGroup = nullptr;
    for (auto *g : groups) {
        if (g->title() == "Multimedia Keys") {
            mmGroup = g;
            break;
        }
    }
    REQUIRE(mmGroup != nullptr);

    auto combos = mmGroup->findChildren<QComboBox *>();
    CHECK_EQ(combos.size(), 8);

    // Each combo should have 9 items (None + 8 actions)
    for (auto *c : combos)
        CHECK_EQ(c->count(), 9);
}

TEST_CASE("PreferencesDialog multimedia keys accept saves bindings")
{
    resetConfig();
    auto &cfg = Config::instance();

    PreferencesDialog dlg;
    auto groups = dlg.findChildren<QGroupBox *>();
    QGroupBox *mmGroup = nullptr;
    for (auto *g : groups) {
        if (g->title() == "Multimedia Keys") {
            mmGroup = g;
            break;
        }
    }
    REQUIRE(mmGroup != nullptr);

    // Find the XF86AudioPlay row by its label
    auto labels = mmGroup->findChildren<QLabel *>();
    QComboBox *playCombo = nullptr;
    for (auto *l : labels) {
        if (l->text().contains("XF86AudioPlay")) {
            // The combo is the next widget in form layout — find by buddy widget
            // In QFormLayout the combo is the 2nd column; we can find via the label's buddy
            // but QFormLayout doesn't set buddy; simpler: iterate combos and match
            // position via QFormLayout index.
            // Simpler: just grab the combo that is the 1st one in the layout order
            // by using QFormLayout directly.
            auto *fl = qobject_cast<QFormLayout *>(mmGroup->layout());
            if (!fl) break;
            for (int i = 0; i < fl->rowCount(); ++i) {
                QLayoutItem *lblItem = fl->itemAt(i, QFormLayout::LabelRole);
                if (lblItem && lblItem->widget() == l) {
                    QLayoutItem *fieldItem = fl->itemAt(i, QFormLayout::FieldRole);
                    if (fieldItem)
                        playCombo = qobject_cast<QComboBox *>(fieldItem->widget());
                    break;
                }
            }
            break;
        }
    }
    REQUIRE(playCombo != nullptr);

    // Change XF86AudioPlay to "Stop"
    playCombo->setCurrentIndex(playCombo->findData(static_cast<int>(MediaKeyAction::Stop)));

    dlg.accept();

    // Verify saved: the binding with keysym 0x1008ff11 should have action Stop
    auto bindings = cfg.mediaKeyBindings();
    auto it = std::find_if(bindings.begin(), bindings.end(),
        [](const MediaKeyBinding &b) { return b.keysym == 0x1008ff11; });
    REQUIRE(it != bindings.end());
    CHECK_EQ(it->action, MediaKeyAction::Stop);
}

// ── Playlist group tests ────────────────────────────────────────────────────

TEST_CASE("PreferencesDialog has playlist group")
{
    resetConfig();
    PreferencesDialog dlg;
    auto groups = dlg.findChildren<QGroupBox *>();
    auto it = std::find_if(groups.begin(), groups.end(),
        [](QGroupBox *g) { return g->title() == "Playlist"; });
    CHECK(it != groups.end());
}

TEST_CASE("PreferencesDialog playlist header markup loads and saves")
{
    resetConfig();
    auto &cfg = Config::instance();
    cfg.setPlaylistHeaderMarkup("$artist — $album");
    cfg.sync();

    PreferencesDialog dlg;
    auto lines = dlg.findChildren<QLineEdit *>();
    QLineEdit *headerLine = nullptr;
    for (auto *l : lines) {
        if (l->placeholderText().contains("markup", Qt::CaseInsensitive) ||
            l->placeholderText().contains("artist", Qt::CaseInsensitive)) {
            headerLine = l;
            break;
        }
    }
    REQUIRE(headerLine != nullptr);
    CHECK_EQ(headerLine->text().toStdString(), "$artist — $album");

    // Modify and save
    headerLine->setText("$title");
    dlg.accept();
    CHECK_EQ(cfg.playlistHeaderMarkup().toStdString(), "$title");
}

TEST_CASE("PreferencesDialog playlist highlight colour button exists")
{
    resetConfig();
    PreferencesDialog dlg;
    auto buttons = dlg.findChildren<QPushButton *>();
    int colorBtnCount = 0;
    for (auto *b : buttons) {
        if (b->toolTip().contains("highlight", Qt::CaseInsensitive))
            ++colorBtnCount;
    }
    CHECK_GE(colorBtnCount, 1);
}

// ── Metadata group tests ────────────────────────────────────────────────────

TEST_CASE("PreferencesDialog has metadata group")
{
    resetConfig();
    PreferencesDialog dlg;
    auto groups = dlg.findChildren<QGroupBox *>();
    auto it = std::find_if(groups.begin(), groups.end(),
        [](QGroupBox *g) { return g->title() == "Metadata"; });
    CHECK(it != groups.end());
}

TEST_CASE("PreferencesDialog metadata font combo is populated")
{
    resetConfig();
    PreferencesDialog dlg;
    auto fontCombos = dlg.findChildren<QFontComboBox *>();
    REQUIRE_EQ(fontCombos.size(), 1);
    CHECK_GE(fontCombos[0]->count(), 1); // At least one font available
}

TEST_CASE("PreferencesDialog metadata colour button exists")
{
    resetConfig();
    PreferencesDialog dlg;
    auto buttons = dlg.findChildren<QPushButton *>();
    int colorBtnCount = 0;
    for (auto *b : buttons) {
        if (b->toolTip().contains("colour", Qt::CaseInsensitive) ||
            b->toolTip().contains("color", Qt::CaseInsensitive))
            ++colorBtnCount;
    }
    CHECK_GE(colorBtnCount, 1);
}

// ── Count migrated items ────────────────────────────────────────────────────

TEST_CASE("PreferencesDialog has at least 8 QGroupBox widgets")
{
    resetConfig();
    PreferencesDialog dlg;
    auto groups = dlg.findChildren<QGroupBox *>();
    // Connection, Playback, Window, Proxy, Server, Tray, Playlist, Metadata, Multimedia Keys, Profiles
    CHECK_GE(groups.size(), 8);
}

// ── Серверные настройки: fading, выходы и ReplayGain (живут у MPD) ───────────
// Транспорт подменён наследником MpdCommands — сеть участвует только в живом кейсе.

namespace {

/// «Сервер» с готовыми ответами; команды записываются для проверки.
class FakeServer : public MpdCommands {
public:
    FakeServer() : MpdCommands(&conn_) {}

    int crossfadeSeconds() override { return fade_; }
    void setCrossfade(int seconds) override
    {
        ++crossfadeCalls;
        lastFade = seconds;
        // Как настоящий сервер: выставленное значение видно следующему чтению,
        // ноль — это выключенный фейд.
        fade_ = qMax(0, seconds);
    }

    int replayGainMode() override { return gain_; }
    void setReplayGainMode(int mode) override
    {
        ++gainCalls;
        lastGain = mode;
        gain_ = mode;
    }

    QVector<MpdOutput> outputs() const override { return devices; }

    void setOutputEnabled(int id, bool on) override
    {
        outputCalls.append(qMakePair(id, on));
        for (MpdOutput &dev : devices)
            if (dev.id == id)
                dev.enabled = on;
    }

    Connection conn_;
    int fade_ = 0;      ///< crossfade сервера (-1 — «нет соединения»)
    int gain_ = 0;      ///< режим ReplayGain
    QVector<MpdOutput> devices;
    QList<QPair<int, bool>> outputCalls;   ///< (id, включить?) по порядку
    int crossfadeCalls = 0;
    int lastFade = -1;
    int gainCalls = 0;
    int lastGain = -1;
};

QGroupBox *liveGroup(PreferencesDialog &dlg)
{
    return dlg.findChild<QGroupBox *>(QStringLiteral("liveServerGroup"));
}

} // namespace

TEST_CASE("группа серверных настроек выключена, пока транспорт не привязан")
{
    resetConfig();
    PreferencesDialog dlg;

    QGroupBox *group = liveGroup(dlg);
    REQUIRE(group != nullptr);
    CHECK_FALSE(group->isEnabled());
    // Сервера нет — окно обязано сказать это словами, а не показать нули.
    auto *hint = dlg.findChild<QLabel *>(QStringLiteral("fadeHint"));
    REQUIRE(hint != nullptr);
    CHECK_EQ(hint->text(), QStringLiteral("Not connected"));
}

TEST_CASE("bindServer заполняет fading, ReplayGain и выходы значениями сервера")
{
    resetConfig();
    FakeServer server;
    server.fade_ = 5;
    server.gain_ = 2;
    server.devices = {{0, QStringLiteral("Speakers"), true},
                      {1, QStringLiteral("Headphones"), false}};

    PreferencesDialog dlg;
    dlg.bindServer(&server);

    CHECK(liveGroup(dlg)->isEnabled());
    auto *enable = dlg.findChild<QCheckBox *>(QStringLiteral("crossfadeEnable"));
    auto *secs   = dlg.findChild<QSpinBox *>(QStringLiteral("crossfadeSec"));
    REQUIRE(enable != nullptr);
    REQUIRE(secs != nullptr);
    CHECK(enable->isChecked());
    CHECK_EQ(secs->value(), 5);
    CHECK(secs->isEnabled());

    auto *gain = dlg.findChild<QComboBox *>(QStringLiteral("replayGainCombo"));
    REQUIRE(gain != nullptr);
    CHECK_EQ(gain->currentIndex(), 2);

    auto *list = dlg.findChild<QListWidget *>(QStringLiteral("outputList"));
    REQUIRE(list != nullptr);
    REQUIRE_EQ(list->count(), 2);
    CHECK_EQ(list->item(0)->text(), QStringLiteral("Speakers"));
    CHECK(list->item(0)->checkState() == Qt::Checked);
    CHECK(list->item(1)->checkState() == Qt::Unchecked);

    // Заливка не считается правкой: ни одной команды серверу.
    CHECK_EQ(server.crossfadeCalls, 0);
    CHECK_EQ(server.gainCalls, 0);
    CHECK(server.outputCalls.isEmpty());
}

TEST_CASE("тумблер выхода шлёт команду по идентификатору, а не по имени")
{
    resetConfig();
    FakeServer server;
    // Одинаковые имена — норма (два «null output» в конфигурации): различает только id.
    server.devices = {{3, QStringLiteral("null output"), false},
                      {7, QStringLiteral("null output"), false}};

    PreferencesDialog dlg;
    dlg.bindServer(&server);

    auto *list = dlg.findChild<QListWidget *>(QStringLiteral("outputList"));
    REQUIRE(list != nullptr);
    list->item(1)->setCheckState(Qt::Checked);

    REQUIRE_EQ(server.outputCalls.size(), 1);
    CHECK_EQ(server.outputCalls.first().first, 7);
    CHECK(server.outputCalls.first().second);
}

TEST_CASE("снятие галочки fading выключает crossfade нулём")
{
    resetConfig();
    FakeServer server;
    server.fade_ = 4;

    PreferencesDialog dlg;
    dlg.bindServer(&server);

    auto *enable = dlg.findChild<QCheckBox *>(QStringLiteral("crossfadeEnable"));
    REQUIRE(enable != nullptr);
    enable->setChecked(false);
    CHECK_EQ(server.lastFade, 0);   // выключение — именно `crossfade 0`
    CHECK_FALSE(dlg.findChild<QSpinBox *>(QStringLiteral("crossfadeSec"))->isEnabled());

    // Обратное включение шлёт сохранённую цифру спина.
    enable->setChecked(true);
    CHECK_EQ(server.lastFade, 4);
}

TEST_CASE("правка времени фейда уходит командой только при включённом тумблере")
{
    resetConfig();
    FakeServer server;

    PreferencesDialog dlg;
    dlg.bindServer(&server);

    auto *secs = dlg.findChild<QSpinBox *>(QStringLiteral("crossfadeSec"));
    REQUIRE(secs != nullptr);
    secs->setValue(9);                       // фейд выключен — команды нет
    CHECK_EQ(server.crossfadeCalls, 0);

    dlg.findChild<QCheckBox *>(QStringLiteral("crossfadeEnable"))->setChecked(true);
    CHECK_EQ(server.lastFade, 9);            // включение отправляет текущую цифру
}

TEST_CASE("смена ReplayGain шлёт режим по индексу списка")
{
    resetConfig();
    FakeServer server;

    PreferencesDialog dlg;
    dlg.bindServer(&server);

    auto *gain = dlg.findChild<QComboBox *>(QStringLiteral("replayGainCombo"));
    REQUIRE(gain != nullptr);
    gain->setCurrentIndex(1);   // Track
    CHECK_EQ(server.lastGain, 1);
}

TEST_CASE("без соединения группа снова гаснет и говорит Not connected")
{
    resetConfig();
    FakeServer server;
    server.fade_ = -1;   // транспорт есть, сервер молчит

    PreferencesDialog dlg;
    dlg.bindServer(&server);

    CHECK_FALSE(liveGroup(dlg)->isEnabled());
    auto *hint = dlg.findChild<QLabel *>(QStringLiteral("fadeHint"));
    REQUIRE(hint != nullptr);
    CHECK_EQ(hint->text(), QStringLiteral("Not connected"));

    // Соединение появилось — перечитывание возвращает группу к жизни.
    server.fade_ = 3;
    QMetaObject::invokeMethod(&dlg, "refreshServerSettings", Qt::DirectConnection);
    CHECK(liveGroup(dlg)->isEnabled());
}

// ── Живой кейс: выходы и crossfade настоящего сервера ───────────────────────

TEST_CASE("живой MPD: окно показывает выходы и меняет их состояние")
{
    const QString socket = qEnvironmentVariable("GMPC_QT_TEST_MPD");
    if (socket.isEmpty()) {
        MESSAGE("GMPC_QT_TEST_MPD не задан — интеграционная проверка пропущена");
        return;
    }

    Connection conn;
    REQUIRE(conn.connectToServer(socket, 0, QString(), 5000));
    MpdCommands cmds(&conn);

    PreferencesDialog dlg;
    dlg.bindServer(&cmds);
    CHECK(liveGroup(dlg)->isEnabled());

    auto *list = dlg.findChild<QListWidget *>(QStringLiteral("outputList"));
    REQUIRE(list != nullptr);
    // В служебной конфигурации объявлен «null output» — список не имеет права быть пустым.
    REQUIRE_GE(list->count(), 1);

    const int id = list->item(0)->data(Qt::UserRole).toInt();
    const bool wasEnabled = list->item(0)->checkState() == Qt::Checked;
    list->item(0)->setCheckState(wasEnabled ? Qt::Unchecked : Qt::Checked);

    // Состояние спрашиваем у сервера отдельным чтением: тумблер мог соврать.
    bool now = !wasEnabled;
    for (const MpdOutput &dev : cmds.outputs())
        if (dev.id == id)
            now = dev.enabled;
    CHECK_EQ(now, !wasEnabled);

    // Возвращаем сервер как был — он служебный и переживёт этот тест.
    list->item(0)->setCheckState(wasEnabled ? Qt::Checked : Qt::Unchecked);
    conn.disconnect();
}

int main(int argc, char *argv[])
{
    doctest::Context ctx(argc, argv);
    QApplication app(argc, argv);
    int result = ctx.run();
    // QApplication destroyed on scope exit BEFORE global statics
    return result;
}
