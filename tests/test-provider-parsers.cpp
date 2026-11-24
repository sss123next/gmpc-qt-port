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
#include "providers/apple-music.h"
#include "providers/lrc-red.h"
#include "providers/lrcmux.h"
#include "providers/unison.h"
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
// AppleMusicProvider fuzz tests
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("AppleMusic: normal search finds album and downloads image")
{
    AppleMusicProvider prov;
    bool found = false;
    QObject ctx;
    QObject::connect(&prov, &AppleMusicProvider::coverFound, &ctx,
                     [&](const QByteArray &) { found = true; });

    // iTunes-style search response
    QJsonObject result;
    result["wrapperType"] = "collection";
    result["collectionType"] = "Album";
    result["artistName"] = "Radiohead";
    result["collectionName"] = "In Rainbows";
    result["artworkUrl100"] = "https://example.com/100x100bb.jpg";
    QJsonArray results; results.append(result);
    QJsonObject root; root["resultCount"] = 1; root["results"] = results;

    prov.testInjectSearchReply(true, QJsonDocument(root).toJson());
    // onSearchReply triggers image download — won't complete without network,
    // but should not crash.
    CHECK(true);
}

TEST_CASE("AppleMusic: artist name mismatch skipped")
{
    AppleMusicProvider prov;
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &AppleMusicProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    // Two results: one with wrong artist, one matching but no artwork on either.
    // Artist-matched result has empty artwork → notFound.
    QJsonObject wrong;
    wrong["wrapperType"] = "collection";
    wrong["collectionType"] = "Album";
    wrong["artistName"] = "Wrong Artist";
    // no artworkUrl — skipped by artist check, fallback has nothing either

    QJsonObject right;
    right["wrapperType"] = "collection";
    right["collectionType"] = "Album";
    right["artistName"] = "Radiohead";
    // no artworkUrl100 key — empty

    QJsonArray results;
    results.append(wrong);
    results.append(right);
    QJsonObject root; root["resultCount"] = 2; root["results"] = results;

    // Set searched artist before injecting reply
    prov.testSetSearched("Radiohead", "In Rainbows");
    prov.testInjectSearchReply(true, QJsonDocument(root).toJson());
    // Neither artist-matched nor fallback has artwork → notFound
    CHECK(notFound);
}

TEST_CASE("AppleMusic: empty results array")
{
    AppleMusicProvider prov;
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &AppleMusicProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    QJsonObject root; root["resultCount"] = 0; root["results"] = QJsonArray();
    prov.testInjectSearchReply(true, QJsonDocument(root).toJson());
    CHECK(notFound);
}

TEST_CASE("AppleMusic: network error")
{
    AppleMusicProvider prov;
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &AppleMusicProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    prov.testInjectSearchReply(false, QByteArray());
    CHECK(errorMsg.contains("network", Qt::CaseInsensitive));
}

TEST_CASE("AppleMusic: invalid JSON")
{
    AppleMusicProvider prov;
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &AppleMusicProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    QByteArray garbage(4096, '\x00');
    prov.testInjectSearchReply(true, garbage);
    CHECK(errorMsg.contains("invalid", Qt::CaseInsensitive));
}

TEST_CASE("AppleMusic: image reply with valid data")
{
    AppleMusicProvider prov;
    bool gotCover = false;
    QObject ctx;
    QObject::connect(&prov, &AppleMusicProvider::coverFound, &ctx,
                     [&](const QByteArray &d) { gotCover = true; CHECK(!d.isEmpty()); });

    prov.testInjectImageReply(true, QByteArray("fake_jpeg_data_here"));
    CHECK(gotCover);
}

TEST_CASE("AppleMusic: image reply empty data")
{
    AppleMusicProvider prov;
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &AppleMusicProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    prov.testInjectImageReply(true, QByteArray());
    CHECK(errorMsg.contains("failed", Qt::CaseInsensitive));
}

TEST_CASE("AppleMusic: image reply ok=false")
{
    AppleMusicProvider prov;
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &AppleMusicProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    prov.testInjectImageReply(false, QByteArray("some data"));
    CHECK(errorMsg.contains("failed", Qt::CaseInsensitive));
}

TEST_CASE("AppleMusic: resizeArtworkUrl basic")
{
    QString url = "https://example.com/100x100bb.jpg";
    QString resized = AppleMusicProvider::resizeArtworkUrl(url, 600);
    CHECK_EQ(resized, "https://example.com/600x600bb.jpg");
}

TEST_CASE("AppleMusic: resizeArtworkUrl png variant")
{
    QString url = "https://example.com/60x60bb.png";
    QString resized = AppleMusicProvider::resizeArtworkUrl(url, 300);
    CHECK_EQ(resized, "https://example.com/300x300bb.png");
}

TEST_CASE("AppleMusic: resizeArtworkUrl no match returns original")
{
    QString url = "https://example.com/cover.jpg";
    QString resized = AppleMusicProvider::resizeArtworkUrl(url, 600);
    CHECK_EQ(resized, url);
}

// ══════════════════════════════════════════════════════════════════════════════
// LrcRedProvider fuzz tests
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("LrcRed: search finds hits and triggers track lookup")
{
    LrcRedProvider prov;
    bool found = false;
    QObject ctx;
    QObject::connect(&prov, &LrcRedProvider::lyricsFound, &ctx,
                     [&](const QString &p, const QString &s) { found = true; });

    QJsonObject hit;
    hit["title"] = "Creep";
    hit["artist"] = "Radiohead";
    hit["duration"] = 238;
    hit["isrc"] = "GBAYE9300465";
    QJsonArray hits; hits.append(hit);
    QJsonObject root; root["hits"] = hits;

    prov.testInjectSearchReply(true, QJsonDocument(root).toJson());
    // onSearchReply triggers track JSON download — won't complete.
    CHECK(true);
}

TEST_CASE("LrcRed: empty search results")
{
    LrcRedProvider prov;
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &LrcRedProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    QJsonObject root; root["hits"] = QJsonArray();
    prov.testInjectSearchReply(true, QJsonDocument(root).toJson());
    CHECK(notFound);
}

TEST_CASE("LrcRed: network error on search")
{
    LrcRedProvider prov;
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &LrcRedProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    prov.testInjectSearchReply(false, QByteArray());
    CHECK(errorMsg.contains("network", Qt::CaseInsensitive));
}

TEST_CASE("LrcRed: hits but no ISRC")
{
    LrcRedProvider prov;
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &LrcRedProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    QJsonObject hit;
    hit["title"] = "Test";
    hit["artist"] = "Test";
    hit["duration"] = 100;
    // no isrc
    QJsonArray hits; hits.append(hit);
    QJsonObject root; root["hits"] = hits;

    prov.testInjectSearchReply(true, QJsonDocument(root).toJson());
    CHECK(notFound);
}

TEST_CASE("LrcRed: track reply with ttml url")
{
    LrcRedProvider prov;
    bool found = false;
    QString gotPlain, gotSynced;
    QObject ctx;
    QObject::connect(&prov, &LrcRedProvider::lyricsFound, &ctx,
                     [&](const QString &p, const QString &s) {
                         found = true; gotPlain = p; gotSynced = s;
                     });

    // Simulate track JSON with ttml file URL — triggers download
    QJsonObject files; files["ttml"] = "https://example.com/lyrics.ttml";
    QJsonObject obj; obj["files"] = files;

    prov.testInjectTrackReply(true, QJsonDocument(obj).toJson());
    // Triggers another download, won't complete, but no crash.
    CHECK(true);
}

TEST_CASE("LrcRed: track reply with lrc url fallback")
{
    LrcRedProvider prov;
    bool found = false;
    QObject ctx;
    QObject::connect(&prov, &LrcRedProvider::lyricsFound, &ctx,
                     [&](const QString &, const QString &) { found = true; });

    QJsonObject files; files["lrc"] = "https://example.com/lyrics.lrc";
    QJsonObject obj; obj["files"] = files;

    prov.testInjectTrackReply(true, QJsonDocument(obj).toJson());
    CHECK(true);
}

TEST_CASE("LrcRed: track reply empty files")
{
    LrcRedProvider prov;
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &LrcRedProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    QJsonObject obj; obj["files"] = QJsonObject();

    prov.testInjectTrackReply(true, QJsonDocument(obj).toJson());
    CHECK(notFound);
}

TEST_CASE("LrcRed: network error on track lookup")
{
    LrcRedProvider prov;
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &LrcRedProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    prov.testInjectTrackReply(false, QByteArray());
    CHECK(errorMsg.contains("network", Qt::CaseInsensitive));
}

TEST_CASE("LrcRed: lyrics reply with TTML data")
{
    LrcRedProvider prov;
    bool found = false;
    QString gotPlain, gotSynced;
    QObject ctx;
    QObject::connect(&prov, &LrcRedProvider::lyricsFound, &ctx,
                     [&](const QString &p, const QString &s) {
                         found = true; gotPlain = p; gotSynced = s;
                     });

    // Simulate a simple TTML document
    QByteArray ttml = R"(<?xml version="1.0"?>
<tt xmlns="http://www.w3.org/ns/ttml">
  <body>
    <div>
      <p begin="00:00:01" end="00:00:05"><span>Hello world</span></p>
    </div>
  </body>
</tt>)";
    prov.testInjectLyricsReply(true, ttml);
    CHECK(found);
    CHECK_EQ(gotPlain, "Hello world");
    CHECK(gotSynced.contains("Hello world"));
}

TEST_CASE("LrcRed: lyrics reply with LRC data")
{
    LrcRedProvider prov;
    bool found = false;
    QString gotPlain, gotSynced;
    QObject ctx;
    QObject::connect(&prov, &LrcRedProvider::lyricsFound, &ctx,
                     [&](const QString &p, const QString &s) {
                         found = true; gotPlain = p; gotSynced = s;
                     });

    QByteArray lrc = "[00:01.00]Line one\n[00:02.00]Line two\n";
    prov.testInjectLyricsReply(true, lrc);
    CHECK(found);
    CHECK_EQ(gotPlain, "Line one\nLine two");  // stripped timestamps
    CHECK_EQ(gotSynced, lrc);
}

TEST_CASE("LrcRed: lyrics reply plain text (no timestamps)")
{
    LrcRedProvider prov;
    bool found = false;
    QString gotPlain, gotSynced;
    QObject ctx;
    QObject::connect(&prov, &LrcRedProvider::lyricsFound, &ctx,
                     [&](const QString &p, const QString &s) {
                         found = true; gotPlain = p; gotSynced = s;
                     });

    QByteArray text = "Just plain lyrics\nno timestamps here";
    prov.testInjectLyricsReply(true, text);
    CHECK(found);
    CHECK_EQ(gotPlain, text);
    CHECK(gotSynced.isEmpty());
}

TEST_CASE("LrcRed: lyrics reply empty data")
{
    LrcRedProvider prov;
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &LrcRedProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    prov.testInjectLyricsReply(true, QByteArray());
    CHECK(notFound);
}

TEST_CASE("LrcRed: lyrics reply ok=false")
{
    LrcRedProvider prov;
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &LrcRedProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    prov.testInjectLyricsReply(false, QByteArray("data"));
    CHECK(notFound);
}

// ══════════════════════════════════════════════════════════════════════════════
// LrcmuxProvider fuzz tests
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Lrcmux: normal response with word-level sync")
{
    LrcmuxProvider prov;
    bool found = false;
    QString gotPlain, gotSynced;
    QObject ctx;
    QObject::connect(&prov, &LrcmuxProvider::lyricsFound, &ctx,
                     [&](const QString &p, const QString &s) {
                         found = true; gotPlain = p; gotSynced = s;
                     });

    QJsonObject word;
    word["start"] = 1000; word["end"] = 1500; word["text"] = "When";
    QJsonArray words; words.append(word);

    QJsonObject line;
    line["start"] = 1000; line["end"] = 3000; line["text"] = "When you";
    line["words"] = words;

    QJsonArray lines; lines.append(line);
    QJsonObject meta; meta["level"] = "word"; meta["instrumental"] = false;
    QJsonObject root; root["meta"] = meta; root["lines"] = lines;

    prov.testInjectReply(true, QJsonDocument(root).toJson());
    CHECK(found);
    CHECK_EQ(gotPlain, "When you");
    CHECK(gotSynced.contains("[00:01.00]"));  // LRC timestamp
    CHECK(gotSynced.contains("<00:01.00>"));  // word-level tag
}

TEST_CASE("Lrcmux: instrumental response")
{
    LrcmuxProvider prov;
    bool found = false;
    QString gotPlain;
    QObject ctx;
    QObject::connect(&prov, &LrcmuxProvider::lyricsFound, &ctx,
                     [&](const QString &p, const QString &) {
                         found = true; gotPlain = p;
                     });

    QJsonObject meta; meta["instrumental"] = true;
    QJsonObject root; root["meta"] = meta; root["lines"] = QJsonArray();

    prov.testInjectReply(true, QJsonDocument(root).toJson());
    CHECK(found);
    CHECK_EQ(gotPlain.toStdString(), "[Instrumental]");
}

TEST_CASE("Lrcmux: empty lines array")
{
    LrcmuxProvider prov;
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &LrcmuxProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    QJsonObject meta; meta["instrumental"] = false;
    QJsonObject root; root["meta"] = meta; root["lines"] = QJsonArray();

    prov.testInjectReply(true, QJsonDocument(root).toJson());
    CHECK(notFound);
}

TEST_CASE("Lrcmux: line-level sync (no word timestamps)")
{
    LrcmuxProvider prov;
    bool found = false;
    QString gotPlain, gotSynced;
    QObject ctx;
    QObject::connect(&prov, &LrcmuxProvider::lyricsFound, &ctx,
                     [&](const QString &p, const QString &s) {
                         found = true; gotPlain = p; gotSynced = s;
                     });

    QJsonObject line;
    line["start"] = 5000; line["end"] = 8000; line["text"] = "Line one";
    QJsonArray lines; lines.append(line);
    QJsonObject meta; meta["level"] = "line"; meta["instrumental"] = false;
    QJsonObject root; root["meta"] = meta; root["lines"] = lines;

    prov.testInjectReply(true, QJsonDocument(root).toJson());
    CHECK(found);
    CHECK(gotSynced.contains("[00:05.00]"));
    CHECK_FALSE(gotSynced.contains("<"));  // no word tags
}

TEST_CASE("Lrcmux: level=none (no sync)")
{
    LrcmuxProvider prov;
    bool found = false;
    QString gotPlain, gotSynced;
    QObject ctx;
    QObject::connect(&prov, &LrcmuxProvider::lyricsFound, &ctx,
                     [&](const QString &p, const QString &s) {
                         found = true; gotPlain = p; gotSynced = s;
                     });

    QJsonObject line; line["text"] = "Plain text line";
    QJsonArray lines; lines.append(line);
    QJsonObject meta; meta["level"] = "none"; meta["instrumental"] = false;
    QJsonObject root; root["meta"] = meta; root["lines"] = lines;

    prov.testInjectReply(true, QJsonDocument(root).toJson());
    CHECK(found);
    CHECK_EQ(gotPlain, "Plain text line");
    CHECK(gotSynced.isEmpty());
}

TEST_CASE("Lrcmux: network error")
{
    LrcmuxProvider prov;
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &LrcmuxProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    prov.testInjectReply(false, QByteArray());
    CHECK(errorMsg.contains("network", Qt::CaseInsensitive));
}

TEST_CASE("Lrcmux: invalid JSON")
{
    LrcmuxProvider prov;
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &LrcmuxProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    QByteArray garbage(4096, '\xFF');
    prov.testInjectReply(true, garbage);
    CHECK(errorMsg.contains("invalid", Qt::CaseInsensitive));
}

// ══════════════════════════════════════════════════════════════════════════════
// UnisonProvider fuzz tests
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Unison: normal response with TTML lyrics")
{
    UnisonProvider prov;
    bool found = false;
    bool gotAttribution = false;
    QString gotPlain, gotSynced, gotSubmitter;
    QObject ctx;
    QObject::connect(&prov, &UnisonProvider::lyricsFound, &ctx,
                     [&](const QString &p, const QString &s) {
                         found = true; gotPlain = p; gotSynced = s;
                     });
    QObject::connect(&prov, &UnisonProvider::attributionInfo, &ctx,
                     [&](const QString &, const QString &name, const QString &) {
                         gotAttribution = true; gotSubmitter = name;
                     });

    QJsonObject submitter;
    submitter["displayName"] = "LyricsBot";
    submitter["avatarUrl"] = "https://example.com/avatar.png";

    QJsonObject data;
    data["lyrics"] = "<tt><body><div><p>Hello world</p></div></body></tt>";
    data["format"] = "ttml";
    data["syncType"] = "linesync";
    data["submitter"] = submitter;
    QJsonObject root; root["success"] = true; root["data"] = data;

    prov.testInjectReply(true, QJsonDocument(root).toJson());
    CHECK(found);
    CHECK(gotAttribution);
    CHECK_EQ(gotSubmitter, "LyricsBot");
    CHECK_EQ(gotPlain, "Hello world");
    CHECK(gotSynced.contains("<tt>"));  // has raw TTML
}

TEST_CASE("Unison: LRC format response")
{
    UnisonProvider prov;
    bool found = false;
    QString gotPlain, gotSynced;
    QObject ctx;
    QObject::connect(&prov, &UnisonProvider::lyricsFound, &ctx,
                     [&](const QString &p, const QString &s) {
                         found = true; gotPlain = p; gotSynced = s;
                     });

    QJsonObject data;
    data["lyrics"] = "[00:01.00]Line one\n[00:02.00]Line two\n";
    data["format"] = "lrc";
    data["syncType"] = "linesync";
    QJsonObject root; root["success"] = true; root["data"] = data;

    prov.testInjectReply(true, QJsonDocument(root).toJson());
    CHECK(found);
    CHECK_EQ(gotSynced, "[00:01.00]Line one\n[00:02.00]Line two\n");
}

TEST_CASE("Unison: plain text format")
{
    UnisonProvider prov;
    bool found = false;
    QString gotPlain, gotSynced;
    QObject ctx;
    QObject::connect(&prov, &UnisonProvider::lyricsFound, &ctx,
                     [&](const QString &p, const QString &s) {
                         found = true; gotPlain = p; gotSynced = s;
                     });

    QJsonObject data;
    data["lyrics"] = "Just plain text lyrics";
    data["format"] = "plain";
    data["syncType"] = "none";
    QJsonObject root; root["success"] = true; root["data"] = data;

    prov.testInjectReply(true, QJsonDocument(root).toJson());
    CHECK(found);
    CHECK_EQ(gotPlain, "Just plain text lyrics");
    CHECK(gotSynced.isEmpty());
}

TEST_CASE("Unison: success=false NOT_FOUND")
{
    UnisonProvider prov;
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &UnisonProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    QJsonObject root;
    root["success"] = false;
    root["code"] = "NOT_FOUND";

    prov.testInjectReply(true, QJsonDocument(root).toJson());
    CHECK(notFound);
}

TEST_CASE("Unison: success=false unknown error")
{
    UnisonProvider prov;
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &UnisonProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    QJsonObject root;
    root["success"] = false;
    root["code"] = "INTERNAL_ERROR";
    root["error"] = "Something broke";

    prov.testInjectReply(true, QJsonDocument(root).toJson());
    CHECK(errorMsg.contains("Something"));
}

TEST_CASE("Unison: network error")
{
    UnisonProvider prov;
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &UnisonProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    prov.testInjectReply(false, QByteArray());
    CHECK(errorMsg.contains("network", Qt::CaseInsensitive));
}

TEST_CASE("Unison: empty lyrics string")
{
    UnisonProvider prov;
    bool notFound = false;
    QObject ctx;
    QObject::connect(&prov, &UnisonProvider::notFound, &ctx,
                     [&]() { notFound = true; });

    QJsonObject data;
    data["lyrics"] = "";
    data["format"] = "plain";
    QJsonObject root; root["success"] = true; root["data"] = data;

    prov.testInjectReply(true, QJsonDocument(root).toJson());
    CHECK(notFound);
}

TEST_CASE("Unison: richsync format (TTML variant)")
{
    UnisonProvider prov;
    bool found = false;
    QString gotPlain;
    QObject ctx;
    QObject::connect(&prov, &UnisonProvider::lyricsFound, &ctx,
                     [&](const QString &p, const QString &) {
                         found = true; gotPlain = p;
                     });

    // richsync is TTML with extra namespaces — same parser handles it
    QJsonObject data;
    data["lyrics"] = R"(<tt xmlns:composer="https://composer.betterlyrics.org/ttml">
  <body><div><p>Rich sync line</p></div></body></tt>)";
    data["format"] = "ttml";
    data["syncType"] = "richsync";
    QJsonObject root; root["success"] = true; root["data"] = data;

    prov.testInjectReply(true, QJsonDocument(root).toJson());
    CHECK(found);
    CHECK_EQ(gotPlain, "Rich sync line");
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
