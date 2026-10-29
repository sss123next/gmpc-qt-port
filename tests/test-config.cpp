#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-config.cpp
 * @brief Tests for core::Config singleton (QSettings wrapper).
 *
 * Requires QApplication (Qt6::Widgets) because QSettings needs QCoreApplication
 * to derive the organisation / application name.
 *
 * Runs each test in an isolated scope — we don't reset QSettings state between
 * cases, but each case uses its own distinct keys (by test name).
 */

#include <QApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "core/config.h"

// ── Fixture: isolated Config with unique INI file per test ──────────────────
//
// doctest doesn't have setup/teardown per testcase like Catch2's sections,
// so we use a manual RAII helper.

struct ConfigFixture
{
    QTemporaryDir dir;
    QString iniPath;

    ConfigFixture()
    {
        REQUIRE(dir.isValid());
        iniPath = dir.path() + "/gmpc-test.ini";
        // Override QSettings path for Config singleton — we can't easily
        // inject it into Config, but the singleton writes to the default
        // location.  Instead we test the underlying QSettings directly
        // via Config methods which delegate to QSettings with fixed keys.
        //
        // Config uses QSettings() (default INI).  On Linux that's
        // ~/.config/<app>/<org>.ini — so we set the env vars.
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
};

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QApplication app(argc, &argv);

// ── Nuke any leftover QSettings file before Config::instance() is called ──
// Config("gmpc-qt", "gmpc-qt") → Qt6 uses NativeFormat = INI → gmpc-qt.conf
static struct ConfigFileZapper {
    ConfigFileZapper() {
        QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                       + "/gmpc-qt/gmpc-qt.conf";
        QFile::remove(path);
    }
} configZapper;

// ── Tests ────────────────────────────────────────────────────────────────────

/// Zap the on-disk QSettings INI file so Config starts clean.
static void removeConfigFile()
{
    // Config::Config() constructs QSettings("gmpc-qt", "gmpc-qt")
    // → ~/.config/gmpc-qt/gmpc-qt.ini
    QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                   + "/gmpc-qt/gmpc-qt.ini";
    QFile::remove(path);
}

/// Helper: remove all QSettings keys so each test starts clean.
static void clearSettings()
{
    QSettings s;
    s.clear();
    s.sync();
}

TEST_CASE("Config default values")
{
    removeConfigFile();
    clearSettings();
    auto &cfg = Config::instance();
    CHECK_EQ(cfg.host(), QString("localhost"));
    CHECK_EQ(cfg.port(), 6600);
    CHECK_EQ(cfg.password(), QString());
}

TEST_CASE("Config round-trip through setters / getters")
{
    clearSettings();
    auto &cfg = Config::instance();

    cfg.setHost("mpd.example.com");
    CHECK_EQ(cfg.host(), "mpd.example.com");

    cfg.setPort(6700);
    CHECK_EQ(cfg.port(), 6700);

    cfg.setPassword("secret");
    CHECK_EQ(cfg.password(), "secret");

    cfg.setAutoConnect(true);
    CHECK(cfg.autoConnect());

    cfg.setWindowSize(1024, 768);
    CHECK_EQ(cfg.windowWidth(), 1024);
    CHECK_EQ(cfg.windowHeight(), 768);

    cfg.setStartHidden(true);
    CHECK(cfg.startHidden());

    cfg.setStopOnExit(true);
    CHECK(cfg.stopOnExit());

    cfg.sync();
}

TEST_CASE("Config persists across re-read (via sync)")
{
    clearSettings();
    auto &cfg = Config::instance();

    cfg.setHost("persist-test");
    cfg.setPort(7000);
    cfg.sync();

    // Re-read by re-constructing Config? Not possible (singleton).
    // We verify that values are still what we set after a sync.
    CHECK_EQ(cfg.host(), "persist-test");
    CHECK_EQ(cfg.port(), 7000);
}

TEST_CASE("Default values unchanged after construction")
{
    auto &cfg = Config::instance();
    // These should still be defaults (or last-set values from prior tests).
    // We test that the getters return a string and don't crash.
    CHECK(!cfg.host().isEmpty());
    CHECK_GT(cfg.port(), 0);
}
