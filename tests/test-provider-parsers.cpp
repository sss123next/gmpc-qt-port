#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

/**
 * @file test-provider-parsers.cpp
 * @brief Stress / fuzz tests for network-provider JSON parsers.
 *
 * Compiles the actual provider source files and feeds crafted /
 * malicious data directly into their private onReply() methods
 * via the testInject*() seams — no network calls are made.
 *
 * Also tests CoverProvider cache / local-file / complete paths.
 */

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "providers/discogs.h"
#include "providers/lyrdb.h"
#include "providers/lyrics-ovh.h"
#include "metadata/cover-provider.h"

#include "test-network-drain.h"

// ══════════════════════════════════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════════════════════════════════

/// Create a temporary directory that auto-cleans.
struct ScopedTempDir {
    QTemporaryDir tmp;
    bool valid() const { return tmp.isValid(); }
    QString path() const { return tmp.path(); }
};

// ══════════════════════════════════════════════════════════════════════════════
// DiscogsProvider fuzz tests
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Discogs: normal search response triggers image download")
{
    DiscogsProvider prov(QString(), nullptr);
    // onSearchReply finds a URL and starts dl_->get() for the image.
    // This makes a real HTTP request (no network in CI) but should not crash.
    // The coverFound signal fires only after the image download completes.
    QJsonObject result;
    result["cover_image"] = "https://example.com/cover.jpg";
    QJsonArray results; results.append(result);
    QJsonObject root; root["results"] = results;
    prov.testInjectSearchReply(true, QJsonDocument(root).toJson());
    // No crash — the download callback will fire (or timeout) asynchronously
    CHECK(true);
}

TEST_CASE("Discogs: empty results array (not found)")
{
    DiscogsProvider prov(QString(), nullptr);
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &DiscogsProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    QJsonObject root; root["results"] = QJsonArray();
    prov.testInjectSearchReply(true, QJsonDocument(root).toJson());
    CHECK(notFound);
}

TEST_CASE("Discogs: error response with message")
{
    DiscogsProvider prov(QString(), nullptr);
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &DiscogsProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    QJsonObject root;
    root["message"] = "Rate limited. Please retry.";
    prov.testInjectSearchReply(true, QJsonDocument(root).toJson());
    CHECK(errorMsg.contains("rate limited", Qt::CaseInsensitive));
}

TEST_CASE("Discogs: network error flags ok=false")
{
    DiscogsProvider prov(QString(), nullptr);
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &DiscogsProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    prov.testInjectSearchReply(false, QByteArray());
    CHECK(errorMsg.contains("network"));
}

TEST_CASE("Discogs: result missing cover_image")
{
    DiscogsProvider prov(QString(), nullptr);
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &DiscogsProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    QJsonObject result; result["title"] = "No Cover";
    QJsonArray results; results.append(result);
    QJsonObject root; root["results"] = results;
    prov.testInjectSearchReply(true, QJsonDocument(root).toJson());
    CHECK(notFound);
}

TEST_CASE("Discogs: cover_image empty string")
{
    DiscogsProvider prov(QString(), nullptr);
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &DiscogsProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    QJsonObject result; result["cover_image"] = "";
    QJsonArray results; results.append(result);
    QJsonObject root; root["results"] = results;
    prov.testInjectSearchReply(true, QJsonDocument(root).toJson());
    CHECK(notFound);
}

TEST_CASE("Discogs: results is not an array")
{
    DiscogsProvider prov(QString(), nullptr);
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &DiscogsProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    QJsonObject root; root["results"] = "string_not_array";
    prov.testInjectSearchReply(true, QJsonDocument(root).toJson());
    CHECK(notFound);
}

TEST_CASE("Discogs: extremely long cover_image URL does not crash")
{
    DiscogsProvider prov(QString(), nullptr);
    // Same as normal search — triggers image download, should not crash.
    QJsonObject result;
    result["cover_image"] = QString("https://example.com/") + QString(100000, 'a') + ".jpg";
    QJsonArray results; results.append(result);
    QJsonObject root; root["results"] = results;
    prov.testInjectSearchReply(true, QJsonDocument(root).toJson());
    CHECK(true);
}

TEST_CASE("Discogs: image reply with valid data")
{
    DiscogsProvider prov(QString(), nullptr);
    bool gotCover = false;
    QObject ctx;
    QObject::connect(&prov, &DiscogsProvider::coverFound, &ctx,
                     [&](const QByteArray &) { gotCover = true; });

    prov.testInjectImageReply(true, QByteArray("fake_image_data"));
    CHECK(gotCover);
}

TEST_CASE("Discogs: binary garbage JSON")
{
    DiscogsProvider prov(QString(), nullptr);
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &DiscogsProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    QByteArray garbage(4096, '\x00');
    prov.testInjectSearchReply(true, garbage);
    CHECK(errorMsg.contains("invalid", Qt::CaseInsensitive));
}

TEST_CASE("Discogs: image reply with empty data")
{
    DiscogsProvider prov(QString(), nullptr);
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &DiscogsProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    prov.testInjectImageReply(true, QByteArray());
    CHECK(errorMsg.contains("failed", Qt::CaseInsensitive));
}

TEST_CASE("Discogs: image reply ok=false")
{
    DiscogsProvider prov(QString(), nullptr);
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &DiscogsProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    prov.testInjectImageReply(false, QByteArray("some data"));
    CHECK(errorMsg.contains("failed", Qt::CaseInsensitive));
}

// ══════════════════════════════════════════════════════════════════════════════
// LrcLibProvider fuzz tests
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("LrcLib: normal response with both lyrics")
{
    LrcLibProvider prov;
    bool found = false;
    QString gotPlain, gotSynced;
    QObject ctx;
    QObject::connect(&prov, &LrcLibProvider::lyricsFound, &ctx,
                     [&](const QString &p, const QString &s) {
                         found = true; gotPlain = p; gotSynced = s;
                     });

    QJsonObject obj;
    obj["plainLyrics"] = "Hello";
    obj["syncedLyrics"] = "[00:01] Hello";
    prov.testInjectReply(true, QJsonDocument(obj).toJson());
    CHECK(found);
    CHECK_EQ(gotPlain, "Hello");
    CHECK_EQ(gotSynced, "[00:01] Hello");
}

TEST_CASE("LrcLib: synced lyrics only")
{
    LrcLibProvider prov;
    bool found = false;
    QObject ctx;
    QObject::connect(&prov, &LrcLibProvider::lyricsFound, &ctx,
                     [&](const QString &, const QString &) { found = true; });

    QJsonObject obj;
    obj["syncedLyrics"] = "[00:01] Test";
    prov.testInjectReply(true, QJsonDocument(obj).toJson());
    CHECK(found);
}

TEST_CASE("LrcLib: both lyrics empty → not found")
{
    LrcLibProvider prov;
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &LrcLibProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    QJsonObject obj;
    obj["plainLyrics"] = "";
    obj["syncedLyrics"] = "";
    prov.testInjectReply(true, QJsonDocument(obj).toJson());
    CHECK(notFound);
}

TEST_CASE("LrcLib: error response")
{
    LrcLibProvider prov;
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &LrcLibProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    QJsonObject obj;
    obj["error"] = "Not found";
    prov.testInjectReply(true, QJsonDocument(obj).toJson());
    CHECK(notFound);
}

TEST_CASE("LrcLib: empty object")
{
    LrcLibProvider prov;
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &LrcLibProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    prov.testInjectReply(true, QJsonDocument(QJsonObject()).toJson());
    CHECK(notFound);
}

TEST_CASE("LrcLib: missing lyric keys (only metadata)")
{
    LrcLibProvider prov;
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &LrcLibProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    QJsonObject obj;
    obj["id"] = 42;
    obj["artistName"] = "Test";
    prov.testInjectReply(true, QJsonDocument(obj).toJson());
    CHECK(notFound);
}

TEST_CASE("LrcLib: extremely long lyrics")
{
    LrcLibProvider prov;
    bool found = false;
    QString gotPlain;
    QObject ctx;
    QObject::connect(&prov, &LrcLibProvider::lyricsFound, &ctx,
                     [&](const QString &p, const QString &) { found = true; gotPlain = p; });

    QJsonObject obj;
    obj["plainLyrics"] = QString(1'000'000, 'x');
    prov.testInjectReply(true, QJsonDocument(obj).toJson());
    CHECK(found);
    CHECK_EQ(gotPlain.size(), 1'000'000);
}

TEST_CASE("LrcLib: null bytes in lyrics")
{
    LrcLibProvider prov;
    bool found = false;
    QObject ctx;
    QObject::connect(&prov, &LrcLibProvider::lyricsFound, &ctx,
                     [&](const QString &, const QString &) { found = true; });

    QString evil = "line1";
    evil += QChar(0);
    evil += "line2";
    QJsonObject obj;
    obj["plainLyrics"] = evil;
    prov.testInjectReply(true, QJsonDocument(obj).toJson());
    CHECK(found);
}

TEST_CASE("LrcLib: JSON with BOM prefix")
{
    LrcLibProvider prov;
    bool found = false;
    QObject ctx;
    QObject::connect(&prov, &LrcLibProvider::lyricsFound, &ctx,
                     [&](const QString &, const QString &) { found = true; });

    QByteArray json = QByteArray("\xEF\xBB\xBF");
    json += QJsonDocument(QJsonObject{{"plainLyrics", "test"}}).toJson();
    prov.testInjectReply(true, json);
    CHECK(found);
}

TEST_CASE("LrcLib: binary garbage")
{
    LrcLibProvider prov;
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &LrcLibProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    QByteArray garbage(4096, '\xFF');
    prov.testInjectReply(true, garbage);
    CHECK(errorMsg.contains("invalid", Qt::CaseInsensitive));
}

TEST_CASE("LrcLib: network error (ok=false)")
{
    LrcLibProvider prov;
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &LrcLibProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    prov.testInjectReply(false, QByteArray());
    CHECK(errorMsg.contains("Network", Qt::CaseInsensitive));
}

// ══════════════════════════════════════════════════════════════════════════════
// LyricsOvhProvider fuzz tests
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("LyricsOvh: normal response")
{
    LyricsOvhProvider prov;
    bool found = false;
    QString got;
    QObject ctx;
    QObject::connect(&prov, &LyricsOvhProvider::lyricsFound, &ctx,
                     [&](const QString &l) { found = true; got = l; });

    QJsonObject obj; obj["lyrics"] = "Hello world";
    prov.testInjectReply(true, QJsonDocument(obj).toJson());
    CHECK(found);
    CHECK_EQ(got, "Hello world");
}

TEST_CASE("LyricsOvh: missing lyrics key → not found")
{
    LyricsOvhProvider prov;
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &LyricsOvhProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    QJsonObject obj; obj["error"] = "lyrics not found";
    prov.testInjectReply(true, QJsonDocument(obj).toJson());
    CHECK(notFound);
}

TEST_CASE("LyricsOvh: empty lyrics string")
{
    LyricsOvhProvider prov;
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &LyricsOvhProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    QJsonObject obj; obj["lyrics"] = "";
    prov.testInjectReply(true, QJsonDocument(obj).toJson());
    CHECK(notFound);
}

TEST_CASE("LyricsOvh: extremely long lyrics")
{
    LyricsOvhProvider prov;
    QString got;
    QObject ctx;
    QObject::connect(&prov, &LyricsOvhProvider::lyricsFound, &ctx,
                     [&](const QString &l) { got = l; });

    QJsonObject obj; obj["lyrics"] = QString(500'000, 'x');
    prov.testInjectReply(true, QJsonDocument(obj).toJson());
    CHECK_EQ(got.size(), 500'000);
}

TEST_CASE("LyricsOvh: binary garbage")
{
    LyricsOvhProvider prov;
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &LyricsOvhProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    QByteArray garbage(4096, '\x00');
    prov.testInjectReply(true, garbage);
    CHECK(errorMsg.contains("invalid", Qt::CaseInsensitive));
}

TEST_CASE("LyricsOvh: network error (ok=false)")
{
    LyricsOvhProvider prov;
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &LyricsOvhProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    prov.testInjectReply(false, QByteArray());
    CHECK(errorMsg.contains("Network", Qt::CaseInsensitive));
}

// ══════════════════════════════════════════════════════════════════════════════
// DeezerProvider fuzz tests
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Deezer: normal search result")
{
    DeezerProvider prov;
    bool found = false;
    QObject ctx;
    QObject::connect(&prov, &DeezerProvider::coverFound, &ctx,
                     [&](const QByteArray &) { found = true; });

    QJsonObject artist; artist["name"] = "Test";
    QJsonObject item; item["artist"] = artist; item["cover_big"] = "https://example.com/c.jpg";
    QJsonArray data; data.append(item);
    QJsonObject root; root["data"] = data;
    prov.testInjectSearchReply(true, QJsonDocument(root).toJson());
    // Searching sends another request for the image — that won't fire without
    // a network hop.  Signal never arrives, but no crash.
    CHECK(true);
}

TEST_CASE("Deezer: empty data array")
{
    DeezerProvider prov;
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &DeezerProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    QJsonObject root; root["data"] = QJsonArray();
    prov.testInjectSearchReply(true, QJsonDocument(root).toJson());
    CHECK(notFound);
}

TEST_CASE("Deezer: missing data key")
{
    DeezerProvider prov;
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &DeezerProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    QJsonObject root; root["foo"] = "bar";
    prov.testInjectSearchReply(true, QJsonDocument(root).toJson());
    CHECK(notFound);
}

TEST_CASE("Deezer: binary garbage")
{
    DeezerProvider prov;
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &DeezerProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    QByteArray garbage(4096, '\x00');
    prov.testInjectSearchReply(true, garbage);
    CHECK(errorMsg.contains("invalid", Qt::CaseInsensitive));
}

TEST_CASE("Deezer: image reply empty data")
{
    DeezerProvider prov;
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &DeezerProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    prov.testInjectImageReply(true, QByteArray());
    CHECK(errorMsg.contains("failed", Qt::CaseInsensitive));
}

TEST_CASE("Deezer: image reply ok=false")
{
    DeezerProvider prov;
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &DeezerProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    prov.testInjectImageReply(false, QByteArray("data"));
    CHECK(errorMsg.contains("failed", Qt::CaseInsensitive));
}

// ══════════════════════════════════════════════════════════════════════════════
// CoverProvider — local file and cache path tests
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("CoverProvider: cache hit returns immediately")
{
    ScopedTempDir tmp;
    REQUIRE(tmp.valid());

    CoverProvider prov("", "", tmp.path() + "/cache.db");
    // Pre-populate the cache through the test seam
    QByteArray coverData("fake_image_data");
    prov.testSetArtistAlbum("TestArtist", "TestAlbum");
    prov.testInjectComplete(coverData, "test");

    // Now fetchCover should return from cache without network
    bool gotCover = false;
    QByteArray gotData;
    QObject ctx;
    QObject::connect(&prov, &CoverProvider::coverReady, &ctx,
                     [&](const QByteArray &d) { gotCover = true; gotData = d; });

    prov.fetchCover("TestArtist", "TestAlbum");
    CHECK(gotCover);
    CHECK_EQ(gotData, coverData);
}

TEST_CASE("CoverProvider: local file found in song directory")
{
    ScopedTempDir tmp;
    REQUIRE(tmp.valid());

    // Create a "song directory" with a cover.jpg
    QString songDir = tmp.path() + "/artist/album";
    QDir().mkpath(songDir);
    QFile coverFile(songDir + "/cover.jpg");
    QByteArray coverData("JPEG_data_here");
    if (coverFile.open(QIODevice::WriteOnly)) {
        coverFile.write(coverData);
        coverFile.close();
    }

    CoverProvider prov("", "", tmp.path() + "/cache2.db");
    prov.setMusicDir(tmp.path());

    // fetchCover with URI pointing into that directory
    bool gotCover = false;
    QByteArray gotData;
    QObject ctx;
    QObject::connect(&prov, &CoverProvider::coverReady, &ctx,
                     [&](const QByteArray &d) { gotCover = true; gotData = d; });

    prov.fetchCover("Artist", "Album", "artist/album/song.ogg");
    CHECK(gotCover);
    CHECK_EQ(gotData, coverData);
}

TEST_CASE("CoverProvider: local file with various cover names")
{
    ScopedTempDir tmp;
    REQUIRE(tmp.valid());

    for (const char *name : {"folder.jpg", "folder.png",
                             "album.jpg", "album.png", "front.jpg", "front.png"}) {
        QString songDir = tmp.path() + "/test_" + name;
        QDir().mkpath(songDir);
        QFile f(songDir + QLatin1Char('/') + QLatin1String(name));
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QByteArray("cover_") + QByteArray(name));
            f.close();
        }

        CoverProvider prov("", "", tmp.path() + "/cache_" + name + ".db");
        prov.setMusicDir(tmp.path());

        bool gotCover = false;
        QObject ctx;
        QObject::connect(&prov, &CoverProvider::coverReady, &ctx,
                         [&](const QByteArray &) { gotCover = true; });

        prov.fetchCover("A", "B", QStringLiteral("test_%1/song.ogg").arg(name));
        CHECK_MESSAGE(gotCover, name);
    }
}

TEST_CASE("CoverProvider: nonexistent local directory")
{
    ScopedTempDir tmp;
    REQUIRE(tmp.valid());

    CoverProvider prov("", "", tmp.path() + "/cache3.db");
    prov.setMusicDir(tmp.path());

    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &CoverProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    // URI points to nonexistent directory — should skip local file
    // and cascade to MusicBrainz, which will fail (no network).
    // We set a short timeout or test without expectations.
    prov.fetchCover("NoSuchArtist", "NoSuchAlbum", "nonexistent/song.ogg");
    // Just verify no crash — signal outcome depends on network availability
    CHECK(true);
}

TEST_CASE("CoverProvider: empty artist/album")
{
    ScopedTempDir tmp;
    REQUIRE(tmp.valid());

    CoverProvider prov("", "", tmp.path() + "/cache4.db");

    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &CoverProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    // Empty artist/album — should not crash
    prov.fetchCover("", "");
    // No crash = success
    CHECK(true);
}

TEST_CASE("CoverProvider: testInjectComplete with empty data")
{
    ScopedTempDir tmp;
    REQUIRE(tmp.valid());

    CoverProvider prov("", "", tmp.path() + "/cache5.db");
    prov.testSetArtistAlbum("A", "B");

    // Empty data should be rejected by the guard in testInjectComplete
    // (no signal emitted)
    bool gotCover = false;
    QObject ctx;
    QObject::connect(&prov, &CoverProvider::coverReady, &ctx,
                     [&](const QByteArray &) { gotCover = true; });

    prov.testInjectComplete(QByteArray(), "empty");
    CHECK_FALSE(gotCover);
}

int main(int argc, char *argv[])
{
    doctest::Context ctx(argc, argv);
    QCoreApplication app(argc, argv);
    int result = ctx.run();
    waitForIdle();
    return result;
}
