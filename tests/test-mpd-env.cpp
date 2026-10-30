#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-mpd-env.cpp
 * @brief Parsing and precedence rules for the MPD_* environment variables.
 *
 * Every case clears the four variables first, then sets only the ones it
 * cares about, so a developer's own MPD_HOST can never leak into a result.
 */

#include <QCoreApplication>

#include "core/mpd-env.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QCoreApplication app(argc, &argv);

namespace {

struct CleanEnv {
    CleanEnv() { clear(); }
    ~CleanEnv() { clear(); }

    static void clear() {
        qputenv("MPD_HOST", "");
        qputenv("MPD_PORT", "");
        qputenv("MPD_PASSWORD", "");
        qputenv("MPD_TIMEOUT", "");
    }
    static void host(const char *v)   { qputenv("MPD_HOST", v); }
    static void port(const char *v)   { qputenv("MPD_PORT", v); }
    static void password(const char *v) { qputenv("MPD_PASSWORD", v); }
    static void timeout(const char *v)  { qputenv("MPD_TIMEOUT", v); }
};

/// Resolves with no caller-supplied values at all.
MpdResolvedSettings fromEnvOnly()
{
    return MpdResolvedSettings(QString(), 0, QString());
}

} // namespace

// ── mpdEnvFromEnvironment() ──────────────────────────────────────────────────

TEST_CASE("MPD_HOST plain hostname")
{
    CleanEnv e;
    e.host("mpd.example.com");
    const MpdEnvSettings s = mpdEnvFromEnvironment();
    CHECK_EQ(s.host, "mpd.example.com");
    CHECK(s.password.isEmpty());
    CHECK_EQ(s.port, 0);
    CHECK_FALSE(s.hostFromSocket);
}

TEST_CASE("blank MPD_HOST counts as unset")
{
    CleanEnv e;
    e.host("   ");
    const MpdEnvSettings s = mpdEnvFromEnvironment();
    CHECK(s.host.isEmpty());
    CHECK(s.password.isEmpty());
}

TEST_CASE("MPD_HOST password@host splits the password out")
{
    CleanEnv e;
    e.host("s3cret@musicbox.local");
    const MpdEnvSettings s = mpdEnvFromEnvironment();
    CHECK_EQ(s.host, "musicbox.local");
    CHECK_EQ(s.password, "s3cret");
}

TEST_CASE("MPD_HOST prefix outranks MPD_PASSWORD")
{
    CleanEnv e;
    e.host("fromhost@musicbox.local");
    e.password("fromvar");
    const MpdEnvSettings s = mpdEnvFromEnvironment();
    CHECK_EQ(s.host, "musicbox.local");
    CHECK_EQ(s.password, "fromhost");
}

TEST_CASE("MPD_PASSWORD used when MPD_HOST has no prefix")
{
    CleanEnv e;
    e.host("musicbox.local");
    e.password("fromvar");
    const MpdEnvSettings s = mpdEnvFromEnvironment();
    CHECK_EQ(s.host, "musicbox.local");
    CHECK_EQ(s.password, "fromvar");
}

TEST_CASE("MPD_HOST filesystem Unix socket")
{
    CleanEnv e;
    e.host("/run/mpd/socket");
    const MpdEnvSettings s = mpdEnvFromEnvironment();
    CHECK_EQ(s.host, "/run/mpd/socket");
    CHECK(s.hostFromSocket);
    CHECK_EQ(s.port, 0);
}

TEST_CASE("MPD_HOST abstract Unix socket keeps its leading @")
{
    CleanEnv e;
    e.host("@mpd-socket");
    const MpdEnvSettings s = mpdEnvFromEnvironment();
    CHECK_EQ(s.host, "@mpd-socket");
    CHECK(s.hostFromSocket);
    // The '@' is the abstract-socket marker, not a password separator.
    CHECK(s.password.isEmpty());
}

TEST_CASE("MPD_HOST host:port")
{
    CleanEnv e;
    e.host("musicbox.local:6700");
    const MpdEnvSettings s = mpdEnvFromEnvironment();
    CHECK_EQ(s.host, "musicbox.local");
    CHECK_EQ(s.port, 6700);
}

TEST_CASE("MPD_HOST bracketed IPv6 with port")
{
    CleanEnv e;
    e.host("[::1]:10000");
    const MpdEnvSettings s = mpdEnvFromEnvironment();
    CHECK_EQ(s.host, "[::1]");
    CHECK_EQ(s.port, 10000);
}

TEST_CASE("bare IPv6 MPD_HOST is not split on its colons")
{
    CleanEnv e;
    e.host("fe80::1");
    const MpdEnvSettings s = mpdEnvFromEnvironment();
    CHECK_EQ(s.host, "fe80::1");
    CHECK_EQ(s.port, 0);
}

TEST_CASE("password@host:port splits both")
{
    CleanEnv e;
    e.host("pw@musicbox.local:6700");
    const MpdEnvSettings s = mpdEnvFromEnvironment();
    CHECK_EQ(s.host, "musicbox.local");
    CHECK_EQ(s.port, 6700);
    CHECK_EQ(s.password, "pw");
}

TEST_CASE("a non-numeric port suffix is left in the host")
{
    CleanEnv e;
    e.host("musicbox.local:http");
    const MpdEnvSettings s = mpdEnvFromEnvironment();
    CHECK_EQ(s.host, "musicbox.local:http");
    CHECK_EQ(s.port, 0);
}

TEST_CASE("MPD_PORT is used when MPD_HOST has no port")
{
    CleanEnv e;
    e.host("musicbox.local");
    e.port("6800");
    CHECK_EQ(mpdEnvFromEnvironment().port, 6800);
}

TEST_CASE("MPD_HOST inline port outranks MPD_PORT")
{
    CleanEnv e;
    e.host("musicbox.local:6700");
    e.port("6800");
    CHECK_EQ(mpdEnvFromEnvironment().port, 6700);
}

TEST_CASE("out-of-range or garbage MPD_PORT is ignored")
{
    CleanEnv e;
    SUBCASE("too large") { e.port("70000"); }
    SUBCASE("zero")      { e.port("0"); }
    SUBCASE("garbage")   { e.port("nope"); }
    SUBCASE("negative")  { e.port("-1"); }
    CHECK_EQ(mpdEnvFromEnvironment().port, 0);
}

TEST_CASE("MPD_TIMEOUT in seconds")
{
    CleanEnv e;
    e.timeout("7");
    CHECK_EQ(mpdEnvFromEnvironment().timeoutMs, 7000);
}

TEST_CASE("MPD_TIMEOUT with ms suffix")
{
    CleanEnv e;
    SUBCASE("lowercase") { e.timeout("250ms"); }
    SUBCASE("uppercase") { e.timeout("250MS"); }
    SUBCASE("spaced")    { e.timeout("250 ms"); }
    CHECK_EQ(mpdEnvFromEnvironment().timeoutMs, 250);
}

TEST_CASE("invalid MPD_TIMEOUT falls back to unset")
{
    CleanEnv e;
    SUBCASE("words")   { e.timeout("soon"); }
    SUBCASE("zero")    { e.timeout("0"); }
    SUBCASE("negative") { e.timeout("-5"); }
    SUBCASE("blank")   { e.timeout("  "); }
    CHECK_EQ(mpdEnvFromEnvironment().timeoutMs, 0);
}

// ── MpdResolvedSettings ──────────────────────────────────────────────────────

TEST_CASE("resolve falls back to the caller defaults with empty env")
{
    CleanEnv e;
    const MpdResolvedSettings r = fromEnvOnly();
    CHECK(r.host().isEmpty());
    CHECK_EQ(r.port(), 0);
    CHECK(r.password().isEmpty());
    CHECK_EQ(r.timeoutMs(), 0);
    CHECK_FALSE(r.isUnixSocket());
    CHECK_FALSE(r.fromEnvironment());
}

TEST_CASE("resolve honours explicit defaults")
{
    CleanEnv e;
    const MpdResolvedSettings r(QString(), 0, QString(),
                               QStringLiteral("fallback"), 6601);
    CHECK_EQ(r.host(), "fallback");
    CHECK_EQ(r.port(), 6601);
    CHECK_FALSE(r.fromEnvironment());
}

TEST_CASE("resolve takes host/port/password/timeout from the environment")
{
    CleanEnv e;
    e.host("musicbox.local");
    e.port("6700");
    e.password("pw");
    e.timeout("3");
    const MpdResolvedSettings r = fromEnvOnly();
    CHECK_EQ(r.host(), "musicbox.local");
    CHECK_EQ(r.port(), 6700);
    CHECK_EQ(r.password(), "pw");
    CHECK_EQ(r.timeoutMs(), 3000);
    CHECK(r.fromEnvironment());
}

TEST_CASE("explicit caller values win over the environment")
{
    CleanEnv e;
    e.host("env.host");
    e.port("6700");
    e.password("envpw");
    const MpdResolvedSettings r(QStringLiteral("typed.host"), 6900,
                               QStringLiteral("typedpw"));
    CHECK_EQ(r.host(), "typed.host");
    CHECK_EQ(r.port(), 6900);
    CHECK_EQ(r.password(), "typedpw");
}

TEST_CASE("an explicit password alone still wins over MPD_PASSWORD")
{
    CleanEnv e;
    e.password("envpw");
    const MpdResolvedSettings r(QString(), 0, QStringLiteral("typedpw"));
    CHECK_EQ(r.password(), "typedpw");
}

TEST_CASE("MPD_TIMEOUT applies even when the host is explicit")
{
    CleanEnv e;
    e.timeout("9");
    const MpdResolvedSettings r(QStringLiteral("typed.host"), 0, QString());
    CHECK_EQ(r.timeoutMs(), 9000);
}

TEST_CASE("resolve detects a Unix socket and zeroes the port")
{
    CleanEnv e;
    e.host("/run/mpd/socket");
    e.port("6700");
    const MpdResolvedSettings r = fromEnvOnly();
    CHECK(r.isUnixSocket());
    CHECK_EQ(r.host(), "/run/mpd/socket");
    CHECK_EQ(r.port(), 0);
}

TEST_CASE("fromConfig keeps a saved socket ahead of MPD_HOST")
{
    CleanEnv e;
    e.host("env.host");
    const MpdResolvedSettings r = MpdResolvedSettings::fromConfig(
        QString(), 0, QString(), QStringLiteral("/run/mpd/socket"));
    CHECK(r.isUnixSocket());
    CHECK_EQ(r.host(), "/run/mpd/socket");
    CHECK_EQ(r.port(), 0);
}

TEST_CASE("fromConfig lets MPD_HOST fill an entirely empty config")
{
    CleanEnv e;
    e.host("env.host");
    e.port("6700");
    e.password("envpw");
    // Nothing was ever saved, so the config fields reach us empty.
    const MpdResolvedSettings r =
        MpdResolvedSettings::fromConfig(QString(), 0, QString(), QString());
    CHECK_EQ(r.host(), "env.host");
    CHECK_EQ(r.port(), 6700);
    CHECK_EQ(r.password(), "envpw");
    CHECK(r.fromEnvironment());
}

TEST_CASE("fromConfig keeps a saved host ahead of MPD_HOST")
{
    CleanEnv e;
    e.host("env.host");
    e.port("6700");
    e.password("envpw");
    const MpdResolvedSettings r = MpdResolvedSettings::fromConfig(
        QStringLiteral("saved.host"), 6900, QStringLiteral("savedpw"),
        QString());
    CHECK_EQ(r.host(), "saved.host");
    CHECK_EQ(r.port(), 6900);
    CHECK_EQ(r.password(), "savedpw");
}

TEST_CASE("an unset config host resolves to empty for libmpdclient")
{
    CleanEnv e;
    const MpdResolvedSettings r =
        MpdResolvedSettings::fromConfig(QString(), 0, QString(), QString());
    // libmpdclient applies its own MPD_HOST / localhost fallback here.
    CHECK(r.host().isEmpty());
    CHECK_EQ(r.port(), 0);
}

TEST_CASE("fromConfig with env disabled keeps the profile target intact")
{
    CleanEnv e;
    e.host("env.host");
    e.port("6700");
    e.password("envpw");
    e.timeout("4");

    const MpdResolvedSettings r = MpdResolvedSettings::fromConfig(
        QStringLiteral("profile.host"), 6900, QStringLiteral("profilepw"),
        QString(), 0, false);
    CHECK_EQ(r.host(), "profile.host");
    CHECK_EQ(r.port(), 6900);
    CHECK_EQ(r.password(), "profilepw");
    CHECK_FALSE(r.fromEnvironment());
    // The timeout stays env-driven: profiles cannot express it.
    CHECK_EQ(r.timeoutMs(), 4000);
}

TEST_CASE("fromConfig prefers a saved timeout over MPD_TIMEOUT")
{
    CleanEnv e;
    e.timeout("4");
    const MpdResolvedSettings r = MpdResolvedSettings::fromConfig(
        QString(), 0, QString(), QString(), 9000);
    CHECK_EQ(r.timeoutMs(), 9000);
}

TEST_CASE("fromConfig falls back to MPD_TIMEOUT when none is saved")
{
    CleanEnv e;
    e.timeout("4");
    const MpdResolvedSettings r =
        MpdResolvedSettings::fromConfig(QString(), 0, QString(), QString(), 0);
    CHECK_EQ(r.timeoutMs(), 4000);
}

TEST_CASE("fromConfig with env disabled still prefers the profile socket")
{
    CleanEnv e;
    e.host("env.host");
    const MpdResolvedSettings r = MpdResolvedSettings::fromConfig(
        QStringLiteral("profile.host"), 6900, QString(),
        QStringLiteral("/run/mpd/sock"), 0, false);
    CHECK(r.isUnixSocket());
    CHECK_EQ(r.host(), "/run/mpd/sock");
    CHECK_EQ(r.port(), 0);
}

TEST_CASE("fromConfig with env disabled leaves an empty host empty")
{
    CleanEnv e;
    e.host("env.host");
    const MpdResolvedSettings r = MpdResolvedSettings::fromConfig(
        QString(), 0, QString(), QString(), 0, false);
    CHECK(r.host().isEmpty());
    CHECK_EQ(r.port(), 0);
}
