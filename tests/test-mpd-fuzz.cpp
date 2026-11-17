#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-mpd-fuzz.cpp
 * @brief Stress / fuzz tests for MPD protocol surface.
 *
 * Tests `mpdBasename()`, `classifyMpdError()`, and the MPD
 * environment-variable parser with crafted / malicious inputs.
 *
 * None of these cases contact a live server.
 */

#include <climits>
#include <cstring>

#include <QCoreApplication>

#include <mpd/client.h>

#include "core/mpd-error.h"
#include "core/mpd-env.h"
#include "browsers/file-system-model.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QCoreApplication app(argc, &argv);

// ══════════════════════════════════════════════════════════════════════════════
// mpdBasename() — path parsing
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("mpdBasename: null byte embedded in path")
{
    // Path with embedded null — Qt should handle it gracefully.
    QString evil = QStringLiteral("normal");
    evil += QChar(0);
    evil += QStringLiteral("file.ogg");
    // Should not crash; behaviour is either "normal" or full string up to null.
    QString result = mpdBasename(evil);
    CHECK_FALSE(result.isEmpty());  // worst case: empty is acceptable, no crash
}

TEST_CASE("mpdBasename: very long path")
{
    // 10 000 characters — potential stack/heap issue
    QString longPath = QString(10000, 'a');
    REQUIRE_EQ(longPath.size(), 10000);
    QString result = mpdBasename(longPath);
    CHECK_EQ(result, longPath);  // no slash → entire string
}

TEST_CASE("mpdBasename: path with only slashes")
{
    CHECK(mpdBasename(QStringLiteral("//")).isEmpty());
    CHECK(mpdBasename(QStringLiteral("///")).isEmpty());
    CHECK(mpdBasename(QStringLiteral("////////////////////")).isEmpty());
}

TEST_CASE("mpdBasename: path traversal patterns")
{
    CHECK_EQ(mpdBasename(QStringLiteral("../artist/..")),
             QStringLiteral(".."));
    CHECK_EQ(mpdBasename(QStringLiteral("../artist/song.ogg")),
             QStringLiteral("song.ogg"));
    CHECK_EQ(mpdBasename(QStringLiteral("../../../../etc/passwd")),
             QStringLiteral("passwd"));
    CHECK_EQ(mpdBasename(QStringLiteral("a/../../../b")),
             QStringLiteral("b"));
}

TEST_CASE("mpdBasename: unicode and special characters")
{
    CHECK_EQ(mpdBasename(QStringLiteral("artist/\u202Esong.ogg")),
             QStringLiteral("\u202Esong.ogg"));
    CHECK_EQ(mpdBasename(QStringLiteral("album/\u200Btrack.flac")),
             QStringLiteral("\u200Btrack.flac"));
    CHECK_EQ(mpdBasename(QStringLiteral("genre/\U0001F3B5music.mp3")),
             QStringLiteral("\U0001F3B5music.mp3"));
}

TEST_CASE("mpdBasename: newlines and control chars in path")
{
    CHECK_EQ(mpdBasename(QStringLiteral("dir\n/song.flac")),
             QStringLiteral("song.flac"));
    CHECK_EQ(mpdBasename(QStringLiteral("dir\tsub/song.flac")),
             QStringLiteral("song.flac"));
    CHECK_EQ(mpdBasename(QStringLiteral("dir\r\n/song.flac")),
             QStringLiteral("song.flac"));
}

TEST_CASE("mpdBasename: single character")
{
    CHECK_EQ(mpdBasename(QStringLiteral("a")), QStringLiteral("a"));
    CHECK_EQ(mpdBasename(QStringLiteral("/")), QString());
}

TEST_CASE("mpdBasename: leading slash patterns")
{
    CHECK_EQ(mpdBasename(QStringLiteral("/artist/song.flac")),
             QStringLiteral("song.flac"));
    CHECK_EQ(mpdBasename(QStringLiteral("/")), QString());
    CHECK_EQ(mpdBasename(QStringLiteral("//")), QString());
}

TEST_CASE("mpdBasename: multiple consecutive slashes")
{
    CHECK_EQ(mpdBasename(QStringLiteral("a//b")), QStringLiteral("b"));
    CHECK_EQ(mpdBasename(QStringLiteral("a///b")), QStringLiteral("b"));
    CHECK_EQ(mpdBasename(QStringLiteral("a/////b")), QStringLiteral("b"));
}

TEST_CASE("mpdBasename: dot segments")
{
    CHECK_EQ(mpdBasename(QStringLiteral("dir/.")), QStringLiteral("."));
    CHECK_EQ(mpdBasename(QStringLiteral("dir/./song.flac")),
             QStringLiteral("song.flac"));
    CHECK_EQ(mpdBasename(QStringLiteral("dir/..")), QStringLiteral(".."));
}

// ══════════════════════════════════════════════════════════════════════════════
// classifyMpdError() — error code classification
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("classifyMpdError: all known error codes")
{
    CHECK(classifyMpdError(MPD_ERROR_SUCCESS) == MpdErrorAction::None);
    CHECK(classifyMpdError(MPD_ERROR_SERVER) == MpdErrorAction::KeepConnection);
    CHECK(classifyMpdError(MPD_ERROR_ARGUMENT) == MpdErrorAction::KeepConnection);
    CHECK(classifyMpdError(MPD_ERROR_STATE) == MpdErrorAction::Reconnect);
    CHECK(classifyMpdError(MPD_ERROR_OOM) == MpdErrorAction::Reconnect);
    CHECK(classifyMpdError(MPD_ERROR_TIMEOUT) == MpdErrorAction::Reconnect);
    CHECK(classifyMpdError(MPD_ERROR_SYSTEM) == MpdErrorAction::Reconnect);
    CHECK(classifyMpdError(MPD_ERROR_RESOLVER) == MpdErrorAction::Reconnect);
    CHECK(classifyMpdError(MPD_ERROR_MALFORMED) == MpdErrorAction::Reconnect);
    CHECK(classifyMpdError(MPD_ERROR_CLOSED) == MpdErrorAction::Reconnect);
}

TEST_CASE("classifyMpdError: unknown codes map to Reconnect")
{
    // Beyond the highest known code (SERVER=9)
    CHECK(classifyMpdError(10) == MpdErrorAction::Reconnect);
    CHECK(classifyMpdError(100) == MpdErrorAction::Reconnect);
    CHECK(classifyMpdError(1000) == MpdErrorAction::Reconnect);
    CHECK(classifyMpdError(INT_MAX) == MpdErrorAction::Reconnect);
    CHECK(classifyMpdError(-1) == MpdErrorAction::Reconnect);
    CHECK(classifyMpdError(INT_MIN) == MpdErrorAction::Reconnect);
}

// ══════════════════════════════════════════════════════════════════════════════
// MpdEnvSettings — environment variable parsing
// ══════════════════════════════════════════════════════════════════════════════

namespace {
struct FuzzEnv {
    FuzzEnv()  { clear(); }
    ~FuzzEnv() { clear(); }
    static void clear() {
        qunsetenv("MPD_HOST");
        qunsetenv("MPD_PORT");
        qunsetenv("MPD_PASSWORD");
        qunsetenv("MPD_TIMEOUT");
    }
};
} // namespace

TEST_CASE("MpdEnvSettings: very long MPD_HOST")
{
    FuzzEnv e;
    // 65536-byte hostname (potential overflow vector)
    QString huge(65536, 'x');
    qputenv("MPD_HOST", huge.toUtf8());
    MpdEnvSettings s = mpdEnvFromEnvironment();
    CHECK_FALSE(s.host.isEmpty());
    CHECK_EQ(s.host.size(), 65536);
}

TEST_CASE("MpdEnvSettings: MPD_HOST with embedded null")
{
    FuzzEnv e;
    QByteArray evil("valid");
    evil.append('\0');
    evil.append("host");
    qputenv("MPD_HOST", evil);
    MpdEnvSettings s = mpdEnvFromEnvironment();
    // Should not crash; Qt may truncate at null
    CHECK_FALSE(s.host.isEmpty());
}

TEST_CASE("MpdEnvSettings: MPD_HOST password injection")
{
    FuzzEnv e;
    // MPD_HOST=password@host — ":" in password stays in the password part
    qputenv("MPD_HOST", "p@ss:w0rd@s@fe.host");
    MpdEnvSettings s = mpdEnvFromEnvironment();
    // splitHostPassword uses last '@' as separator:
    //   password = everything before last '@' = "p@ss:w0rd@s"
    //   host = everything after last '@' = "fe.host"
    CHECK_EQ(s.host, "fe.host");
    CHECK_EQ(s.password, "p@ss:w0rd@s");
}

TEST_CASE("MpdEnvSettings: MPD_PORT overflow")
{
    FuzzEnv e;
    qputenv("MPD_PORT", "99999999999999999999");
    MpdEnvSettings s = mpdEnvFromEnvironment();
    CHECK_EQ(s.port, 0);  // out of range → ignored
}

TEST_CASE("MpdEnvSettings: MPD_PORT negative")
{
    FuzzEnv e;
    qputenv("MPD_PORT", "-1");
    MpdEnvSettings s = mpdEnvFromEnvironment();
    CHECK_EQ(s.port, 0);
}

TEST_CASE("MpdEnvSettings: MPD_PORT huge negative")
{
    FuzzEnv e;
    qputenv("MPD_PORT", "-2147483648");
    MpdEnvSettings s = mpdEnvFromEnvironment();
    CHECK_EQ(s.port, 0);
}

TEST_CASE("MpdEnvSettings: MPD_PORT with leading zeros")
{
    FuzzEnv e;
    qputenv("MPD_PORT", "0000006600");
    MpdEnvSettings s = mpdEnvFromEnvironment();
    CHECK_EQ(s.port, 6600);
}

TEST_CASE("MpdEnvSettings: MPD_PORT with whitespace is trimmed and valid")
{
    FuzzEnv e;
    qputenv("MPD_PORT", "  6601  ");
    MpdEnvSettings s = mpdEnvFromEnvironment();
    // readEnv trims whitespace, so "6601" is valid
    CHECK_EQ(s.port, 6601);
}

TEST_CASE("MpdEnvSettings: MPD_TIMEOUT with overflow")
{
    FuzzEnv e;
    qputenv("MPD_TIMEOUT", "99999999999999999");
    MpdEnvSettings s = mpdEnvFromEnvironment();
    CHECK_EQ(s.timeoutMs, 0);  // overflow → ignored
}

TEST_CASE("MpdEnvSettings: MPD_TIMEOUT boundary values")
{
    FuzzEnv e;
    qputenv("MPD_TIMEOUT", "1");
    CHECK_EQ(mpdEnvFromEnvironment().timeoutMs, 1000);

    qputenv("MPD_TIMEOUT", "2147483");  // ~24 days
    CHECK_GT(mpdEnvFromEnvironment().timeoutMs, 0);
}

TEST_CASE("MpdEnvSettings: MPD_TIMEOUT special suffixes")
{
    FuzzEnv e;
    qputenv("MPD_TIMEOUT", " 300 ms ");
    CHECK_EQ(mpdEnvFromEnvironment().timeoutMs, 300);

    qputenv("MPD_TIMEOUT", "500mS");
    CHECK_EQ(mpdEnvFromEnvironment().timeoutMs, 500);

    qputenv("MPD_TIMEOUT", "soon");
    CHECK_EQ(mpdEnvFromEnvironment().timeoutMs, 0);
}

TEST_CASE("MpdEnvSettings: MPD_HOST with unicode")
{
    FuzzEnv e;
    qputenv("MPD_HOST", "m\u00FCsik.example.com");
    MpdEnvSettings s = mpdEnvFromEnvironment();
    CHECK(s.host.contains("example.com"));
}

TEST_CASE("MpdEnvSettings: MPD_HOST password@[IPv6]:port")
{
    FuzzEnv e;
    qputenv("MPD_HOST", "p%40ss@[::1]:6700");
    MpdEnvSettings s = mpdEnvFromEnvironment();
    // last '@' splits: password="p%40ss", host="[::1]:6700"
    // then host port extraction: bracketed [::1], colon after bracket → port 6700
    CHECK_EQ(s.host, "[::1]");
    CHECK_EQ(s.port, 6700);
    CHECK_EQ(s.password, "p%40ss");
}

TEST_CASE("MpdEnvSettings: MPD_HOST multiple @ — last is separator")
{
    FuzzEnv e;
    qputenv("MPD_HOST", "a@b@host");
    MpdEnvSettings s = mpdEnvFromEnvironment();
    // last '@' at position 2 → password="a@b", host="host"
    CHECK_EQ(s.host, "host");
    CHECK_EQ(s.password, "a@b");
}

TEST_CASE("MpdEnvSettings: MPD_HOST @ prefix is abstract socket, not password")
{
    FuzzEnv e;
    qputenv("MPD_HOST", "@host");
    MpdEnvSettings s = mpdEnvFromEnvironment();
    // '@' at index 0 means abstract socket — no password split
    CHECK_EQ(s.host, "@host");
    CHECK(s.hostFromSocket);
    CHECK(s.password.isEmpty());
}

TEST_CASE("MpdEnvSettings: MPD_HOST colon in password")
{
    FuzzEnv e;
    qputenv("MPD_HOST", "p:w@host:6700");
    MpdEnvSettings s = mpdEnvFromEnvironment();
    // last '@' splits: password="p:w", host="host:6700"
    // host port extraction: colon after host → port 6700
    CHECK_EQ(s.host, "host");
    CHECK_EQ(s.port, 6700);
    CHECK_EQ(s.password, "p:w");
}

// ══════════════════════════════════════════════════════════════════════════════
// MpdResolvedSettings — combined config + env resolution
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("MpdResolvedSettings: explicit host with unicode password")
{
    MpdResolvedSettings r(QStringLiteral("音乐.server"),
                          6600,
                          QStringLiteral("パスワード"));
    CHECK_EQ(r.host(), "音乐.server");
    CHECK_EQ(r.port(), 6600);
    CHECK_EQ(r.password(), "パスワード");
    CHECK_FALSE(r.isUnixSocket());
}

TEST_CASE("MpdResolvedSettings: zero port means library default")
{
    MpdResolvedSettings r(QStringLiteral("host"), 0, QString());
    CHECK_EQ(r.port(), 0);
}

TEST_CASE("MpdResolvedSettings: timeoutMs preservation")
{
    MpdResolvedSettings r(QString(), 0, QString());
    CHECK_EQ(r.timeoutMs(), 0);

    MpdResolvedSettings r2(QString(), 0, QString(), QString(), 0, 5000);
    CHECK_EQ(r2.timeoutMs(), 5000);
}

TEST_CASE("MpdResolvedSettings: socket path injection")
{
    MpdResolvedSettings r = MpdResolvedSettings::fromConfig(
        QString(), 0, QString(),
        QStringLiteral("/run/mpd/../socket"));
    CHECK(r.isUnixSocket());
    // Path should remain as-is (caller's responsibility to normalize)
    CHECK_EQ(r.host(), "/run/mpd/../socket");
}
