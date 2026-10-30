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
#include <QSpinBox>

#include "gui/preferences-dialog.h"
#include "core/config.h"

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

int main(int argc, char *argv[])
{
    doctest::Context ctx(argc, argv);
    QApplication app(argc, argv);
    int result = ctx.run();
    // QApplication destroyed on scope exit BEFORE global statics
    return result;
}
