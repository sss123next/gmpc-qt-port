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
#include <QGroupBox>
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

int main(int argc, char *argv[])
{
    doctest::Context ctx(argc, argv);
    QApplication app(argc, argv);
    int result = ctx.run();
    // QApplication destroyed on scope exit BEFORE global statics
    return result;
}
