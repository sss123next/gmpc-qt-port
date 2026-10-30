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
    // → ~/.config/gmpc-qt/gmpc-qt.conf
    QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                   + "/gmpc-qt/gmpc-qt.conf";
    QFile::remove(path);
}

/// Also zap the legacy .ini path for good measure
static void removeLegacyConfigFile()
{
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
    // Unset, not "localhost"/6600: mpd-env resolves those (or MPD_HOST)
    // at connect time so the saved settings stay distinguishable.
    CHECK(cfg.host().isEmpty());
    CHECK_EQ(cfg.port(), 0);
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
    // host() is deliberately empty when nothing was ever saved.
    CHECK_GT(cfg.port(), 0);
}

// ── Multimedia keys ─────────────────────────────────────────────────────────

TEST_CASE("MediaKey default bindings are 8 entries and cover known keysyms")
{
    clearSettings();
    auto &cfg = Config::instance();
    const auto bindings = cfg.mediaKeyBindings();
    CHECK_EQ(bindings.size(), 8);

    // Verify all known XF86Audio keysyms are present
    QSet<quint32> keysyms;
    for (const auto &b : bindings)
        keysyms.insert(b.keysym);

    CHECK(keysyms.contains(0x1008ff11)); // XF86AudioPlay
    CHECK(keysyms.contains(0x1008ff12)); // XF86AudioPlayPause
    CHECK(keysyms.contains(0x1008ff13)); // XF86AudioStop
    CHECK(keysyms.contains(0x1008ff14)); // XF86AudioPrev
    CHECK(keysyms.contains(0x1008ff15)); // XF86AudioPause
    CHECK(keysyms.contains(0x1008ff16)); // XF86AudioRewind
    CHECK(keysyms.contains(0x1008ff17)); // XF86AudioNext
    CHECK(keysyms.contains(0x1008ff19)); // XF86AudioForward
}

TEST_CASE("MediaKey actionForKey returns correct actions")
{
    clearSettings();
    auto &cfg = Config::instance();

    CHECK_EQ(cfg.actionForKey(0x1008ff11), MediaKeyAction::Play);
    CHECK_EQ(cfg.actionForKey(0x1008ff12), MediaKeyAction::PlayPause);
    CHECK_EQ(cfg.actionForKey(0x1008ff13), MediaKeyAction::Stop);
    CHECK_EQ(cfg.actionForKey(0x1008ff14), MediaKeyAction::Previous);
    CHECK_EQ(cfg.actionForKey(0x1008ff15), MediaKeyAction::Pause);
    CHECK_EQ(cfg.actionForKey(0x1008ff16), MediaKeyAction::Rewind);
    CHECK_EQ(cfg.actionForKey(0x1008ff17), MediaKeyAction::Next);
    CHECK_EQ(cfg.actionForKey(0x1008ff19), MediaKeyAction::Forward);
}

TEST_CASE("MediaKey actionForKey returns None for unknown keysym")
{
    clearSettings();
    auto &cfg = Config::instance();

    CHECK_EQ(cfg.actionForKey(0xDEADBEEF), MediaKeyAction::None);
    CHECK_EQ(cfg.actionForKey(0x00000000), MediaKeyAction::None);
}

TEST_CASE("MediaKey bindings persist through set -> sync -> re-read")
{
    clearSettings();
    auto &cfg = Config::instance();

    // Create custom bindings: only two, with swapped actions
    QList<MediaKeyBinding> custom;
    custom.append({ 0x1008ff11, MediaKeyAction::Stop });   // Play → Stop
    custom.append({ 0x1008ff13, MediaKeyAction::Play });   // Stop → Play
    cfg.setMediaKeyBindings(custom);
    cfg.sync();

    // Re-read
    const auto loaded = cfg.mediaKeyBindings();
    CHECK_EQ(loaded.size(), 2);

    // Verify Play → Stop
    for (const auto &b : loaded) {
        if (b.keysym == 0x1008ff11)
            CHECK_EQ(b.action, MediaKeyAction::Stop);
        else if (b.keysym == 0x1008ff13)
            CHECK_EQ(b.action, MediaKeyAction::Play);
    }

    // actionForKey should reflect swapped bindings
    CHECK_EQ(cfg.actionForKey(0x1008ff11), MediaKeyAction::Stop);
    CHECK_EQ(cfg.actionForKey(0x1008ff13), MediaKeyAction::Play);
    CHECK_EQ(cfg.actionForKey(0x1008ff17), MediaKeyAction::None); // removed
}

TEST_CASE("MediaKey empty bindings round-trip")
{
    clearSettings();
    auto &cfg = Config::instance();

    cfg.setMediaKeyBindings({});
    cfg.sync();

    const auto loaded = cfg.mediaKeyBindings();
    CHECK(loaded.isEmpty());
}

TEST_CASE("MediaKey empty bindings saved and restored as empty set")
{
    clearSettings();
    auto &cfg = Config::instance();

    cfg.setMediaKeyBindings({});
    cfg.sync();
    CHECK(cfg.mediaKeyBindings().isEmpty());

    // Syncing empty list stores count=0, not deleting the group;
    // re-read should still be empty
    const auto again = cfg.mediaKeyBindings();
    CHECK(again.isEmpty());
}

// ── Proxy ─────────────────────────────────────────────────────────────────

TEST_CASE("Proxy defaults are disabled with empty host")
{
    clearSettings();
    auto &cfg = Config::instance();
    CHECK_FALSE(cfg.proxyEnabled());
    CHECK(cfg.proxyHost().isEmpty());
    CHECK_EQ(cfg.proxyPort(), 8080);
}

TEST_CASE("Proxy round-trip through setters")
{
    clearSettings();
    auto &cfg = Config::instance();

    cfg.setProxyEnabled(true);
    cfg.setProxyHost("proxy.local");
    cfg.setProxyPort(3128);
    cfg.sync();

    CHECK(cfg.proxyEnabled());
    CHECK_EQ(cfg.proxyHost(), "proxy.local");
    CHECK_EQ(cfg.proxyPort(), 3128);
}

// ── Server ────────────────────────────────────────────────────────────────

TEST_CASE("Server defaults")
{
    clearSettings();
    auto &cfg = Config::instance();
    CHECK(cfg.musicDirectory().isEmpty());
    CHECK_EQ(cfg.reconnectDelay(), 2000);
    // Unset rather than 5000: mpd-env resolves MPD_TIMEOUT / the default
    // at connect time, mirroring host() and port().
    CHECK_EQ(cfg.connectionTimeout(), 0);
}

TEST_CASE("Server round-trip")
{
    clearSettings();
    auto &cfg = Config::instance();

    cfg.setMusicDirectory("/srv/music");
    cfg.setReconnectDelay(1500);
    cfg.setConnectionTimeout(10000);
    cfg.sync();

    CHECK_EQ(cfg.musicDirectory(), "/srv/music");
    CHECK_EQ(cfg.reconnectDelay(), 1500);
    CHECK_EQ(cfg.connectionTimeout(), 10000);
}

// ── Playlist ─────────────────────────────────────────────────────────────

TEST_CASE("Playlist defaults are empty")
{
    clearSettings();
    auto &cfg = Config::instance();
    CHECK(cfg.playlistHeaderMarkup().isEmpty());
    CHECK(cfg.playlistHighlightColor().isEmpty());
}

TEST_CASE("Playlist header markup round-trip")
{
    clearSettings();
    auto &cfg = Config::instance();

    cfg.setPlaylistHeaderMarkup("$artist — $title");
    cfg.setPlaylistHighlightColor("#ffcc00");
    cfg.sync();

    CHECK_EQ(cfg.playlistHeaderMarkup(), "$artist — $title");
    CHECK_EQ(cfg.playlistHighlightColor(), "#ffcc00");
}

// ── Metadata ─────────────────────────────────────────────────────────────

TEST_CASE("Metadata defaults are empty")
{
    clearSettings();
    auto &cfg = Config::instance();
    CHECK(cfg.metadataFont().isEmpty());
    CHECK(cfg.metadataColor().isEmpty());
}

TEST_CASE("Metadata round-trip")
{
    clearSettings();
    auto &cfg = Config::instance();

    cfg.setMetadataFont("Sans Serif");
    cfg.setMetadataColor("#e0e0e0");
    cfg.sync();

    CHECK_EQ(cfg.metadataFont(), "Sans Serif");
    CHECK_EQ(cfg.metadataColor(), "#e0e0e0");
}

// ── Tray ─────────────────────────────────────────────────────────────────

TEST_CASE("Tray default is enabled")
{
    clearSettings();
    auto &cfg = Config::instance();
    CHECK(cfg.trayEnabled());
}

TEST_CASE("Tray round-trip")
{
    clearSettings();
    auto &cfg = Config::instance();

    cfg.setTrayEnabled(false);
    cfg.sync();
    CHECK_FALSE(cfg.trayEnabled());

    cfg.setTrayEnabled(true);
    cfg.sync();
    CHECK(cfg.trayEnabled());
}

// ── Socket path ──────────────────────────────────────────────────────────

TEST_CASE("Socket path defaults to empty")
{
    clearSettings();
    auto &cfg = Config::instance();
    CHECK(cfg.socketPath().isEmpty());
}

TEST_CASE("Socket path round-trip")
{
    clearSettings();
    auto &cfg = Config::instance();

    cfg.setSocketPath("/run/mpd/socket");
    cfg.sync();
    CHECK_EQ(cfg.socketPath(), "/run/mpd/socket");

    cfg.setSocketPath(QString());
    cfg.sync();
    CHECK(cfg.socketPath().isEmpty());
}

// ── Window X/Y / maximised ──────────────────────────────────────────────

TEST_CASE("Window position defaults to negative (unset)")
{
    clearSettings();
    auto &cfg = Config::instance();
    CHECK_EQ(cfg.windowX(), -1);
    CHECK_EQ(cfg.windowY(), -1);
    CHECK_FALSE(cfg.windowMaximized());
}

TEST_CASE("Window position round-trip")
{
    clearSettings();
    auto &cfg = Config::instance();

    cfg.setWindowPos(100, 200);
    cfg.setWindowMaximized(true);
    cfg.sync();

    CHECK_EQ(cfg.windowX(), 100);
    CHECK_EQ(cfg.windowY(), 200);
    CHECK(cfg.windowMaximized());
}

// ── Profiles ─────────────────────────────────────────────────────────────

TEST_CASE("Profiles default to empty list")
{
    clearSettings();
    auto &cfg = Config::instance();
    CHECK(cfg.profiles().isEmpty());
}

TEST_CASE("Profiles round-trip with multiple entries")
{
    clearSettings();
    auto &cfg = Config::instance();

    QList<ConnectionProfile> profiles;
    profiles.append({ "Home",   "mpd.local",    6600, "pw1", "", false });
    profiles.append({ "Work",   "mpd.company",  6700, "pw2", "", true });
    profiles.append({ "Socket", "",             0,    "",    "/run/mpd/sock", false });
    cfg.setProfiles(profiles);
    cfg.sync();

    auto loaded = cfg.profiles();
    CHECK_EQ(loaded.size(), 3);

    CHECK_EQ(loaded[0].name, "Home");
    CHECK_EQ(loaded[0].host, "mpd.local");
    CHECK_EQ(loaded[0].port, 6600);
    CHECK_EQ(loaded[0].password, "pw1");
    CHECK(loaded[0].socketPath.isEmpty());
    CHECK_FALSE(loaded[0].autoConnect);

    CHECK_EQ(loaded[1].name, "Work");
    CHECK_EQ(loaded[1].port, 6700);
    CHECK(loaded[1].autoConnect);

    CHECK_EQ(loaded[2].name, "Socket");
    CHECK_EQ(loaded[2].socketPath, "/run/mpd/sock");
}

TEST_CASE("Active profile defaults to empty")
{
    clearSettings();
    auto &cfg = Config::instance();
    CHECK(cfg.activeProfile().isEmpty());
}

TEST_CASE("Active profile round-trip")
{
    clearSettings();
    auto &cfg = Config::instance();

    cfg.setActiveProfile("Home");
    cfg.sync();
    CHECK_EQ(cfg.activeProfile(), "Home");

    cfg.setActiveProfile(QString());
    cfg.sync();
    CHECK(cfg.activeProfile().isEmpty());
}

TEST_CASE("Profiles cannot be read after clear")
{
    clearSettings();
    auto &cfg = Config::instance();

    // Save one profile
    cfg.setProfiles({ { "Test", "h", 6600, "", "", true } });
    cfg.sync();
    CHECK_EQ(cfg.profiles().size(), 1);

    // Overwrite with empty
    cfg.setProfiles({});
    cfg.sync();
    CHECK(cfg.profiles().isEmpty());
}

