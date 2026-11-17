#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-tag-reader-fuzz.cpp
 * @brief Stress / fuzz tests for local file tag reader.
 *
 * Generates temporary files with crafted / malicious content and passes
 * them through readEmbeddedCover() and readEmbeddedLyrics().
 *
 * Tests:
 *   - scanEmbeddedID3v2() raw byte scanning
 *   - Format-specific parsers with corrupt data
 *   - CacheSqlite with SQL injection vectors
 */

#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QByteArray>
#include <QDebug>

#include "metadata/tag-reader.h"
#include "metadata/cache-sqlite.h"

// ══════════════════════════════════════════════════════════════════════════════
// Helpers: create small binary files with specific content
// ══════════════════════════════════════════════════════════════════════════════

/// Write @a data to a temporary file and return its absolute path.
static QString createTempFile(const QByteArray &data)
{
    QTemporaryFile tmp;
    tmp.setAutoRemove(false);
    if (!tmp.open()) return {};
    tmp.write(data);
    tmp.flush();
    const QString path = tmp.fileName();
    tmp.close();
    return path;
}

/// Write @a data to @a path.
static void writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(data);
}

/// Remove file at @a path.
static void removeFile(const QString &path)
{
    QFile::remove(path);
}

// ══════════════════════════════════════════════════════════════════════════════
// scanEmbeddedID3v2 — raw ID3v2 scanning
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("scanEmbeddedID3v2: empty file")
{
    QByteArray empty;
    const QString path = createTempFile(empty);
    EmbeddedCover c = readEmbeddedCover(path);
    CHECK(c.data.isEmpty());  // no crash on empty file
    removeFile(path);
}

TEST_CASE("scanEmbeddedID3v2: minimal valid ID3v2 header")
{
    // Build smallest valid ID3v2.4 header + tag (10 byte header + 0 byte body)
    QByteArray id3;
    id3.append("ID3", 3);           // magic
    id3.append('\x04');             // version 2.4
    id3.append('\x00');             // flags (none)
    id3.append('\x00');             // syncsafe size: 0
    id3.append('\x00');
    id3.append('\x00');
    id3.append('\x00');
    // Total: 10 bytes, tag size = 0

    const QString path = createTempFile(id3);
    EmbeddedCover c = readEmbeddedCover(path);
    CHECK(c.data.isEmpty());  // valid header but no frames → no cover
    removeFile(path);
}

TEST_CASE("scanEmbeddedID3v2: valid header with minimal APIC frame")
{
    // Build an ID3v2.4 tag with a minimal APIC frame (no picture data)
    // This tests TagLib's parser with an edge-case APIC
    QByteArray id3;
    id3.append("ID3", 3);
    id3.append('\x04');
    id3.append('\x00');

    // APIC frame header: frame ID "APIC" + 4-byte size + 2-byte flags
    QByteArray framePayload;
    // Text encoding (1 byte) = 0x00 (ISO-8859-1)
    framePayload.append('\x00');
    // MIME type: "image/jpeg\0"
    framePayload.append("image/jpeg");
    framePayload.append('\x00');
    // Picture type: 0x03 (cover front)
    framePayload.append('\x03');
    // Description: empty (0x00)
    framePayload.append('\x00');
    // Picture data: empty (0 bytes)

    // Frame size = payload size
    quint32 frameSize = framePayload.size();
    QByteArray frameHeader;
    frameHeader.append("APIC");
    frameHeader.append(static_cast<char>((frameSize >> 24) & 0xFF));
    frameHeader.append(static_cast<char>((frameSize >> 16) & 0xFF));
    frameHeader.append(static_cast<char>((frameSize >> 8) & 0xFF));
    frameHeader.append(static_cast<char>(frameSize & 0xFF));
    frameHeader.append('\x00');  // flags high
    frameHeader.append('\x00');  // flags low

    QByteArray tagBody = frameHeader + framePayload;
    // Syncsafe size
    quint32 tagDataSize = tagBody.size();
    id3.append(static_cast<char>((tagDataSize >> 21) & 0x7F));
    id3.append(static_cast<char>((tagDataSize >> 14) & 0x7F));
    id3.append(static_cast<char>((tagDataSize >> 7) & 0x7F));
    id3.append(static_cast<char>(tagDataSize & 0x7F));
    id3.append(tagBody);

    const QString path = createTempFile(id3);
    EmbeddedCover c = readEmbeddedCover(path);
    // Should parse without crash — APIC exists but has no picture data
    CHECK(c.data.isEmpty());
    removeFile(path);
}

TEST_CASE("scanEmbeddedID3v2: file shorter than ID3v2 header")
{
    // File with "ID3" but less than 10 bytes
    QByteArray shortId3("ID3\x05", 4);
    const QString path = createTempFile(shortId3);
    EmbeddedCover c = readEmbeddedCover(path);
    CHECK(c.data.isEmpty());
    removeFile(path);
}

TEST_CASE("scanEmbeddedID3v2: only the magic bytes")
{
    QByteArray justMagic("ID3", 3);
    const QString path = createTempFile(justMagic);
    EmbeddedCover c = readEmbeddedCover(path);
    CHECK(c.data.isEmpty());
    removeFile(path);
}

TEST_CASE("scanEmbeddedID3v2: non-syncsafe size field")
{
    // Valid "ID3" magic but size field has MSB set (not syncsafe)
    QByteArray id3;
    id3.append("ID3", 3);
    id3.append('\x04');
    id3.append('\x00');
    id3.append('\x80');  // MSB set — not syncsafe!
    id3.append('\x00');
    id3.append('\x00');
    id3.append('\x00');

    const QString path = createTempFile(id3);
    EmbeddedCover c = readEmbeddedCover(path);
    CHECK(c.data.isEmpty());  // should reject non-syncsafe
    removeFile(path);
}

TEST_CASE("scanEmbeddedID3v2: all syncsafe bytes have MSB set")
{
    // All 4 size bytes have MSB=1
    QByteArray id3;
    id3.append("ID3", 3);
    id3.append('\x04');
    id3.append('\x00');
    id3.append('\x81');
    id3.append('\x81');
    id3.append('\x81');
    id3.append('\x81');

    const QString path = createTempFile(id3);
    EmbeddedCover c = readEmbeddedCover(path);
    CHECK(c.data.isEmpty());
    removeFile(path);
}

TEST_CASE("scanEmbeddedID3v2: extremely large syncsafe size")
{
    // Syncsafe size that would exceed file size
    QByteArray id3;
    id3.append("ID3", 3);
    id3.append('\x04');
    id3.append('\x00');
    // Max valid syncsafe = (0x7F << 21) | (0x7F << 14) | (0x7F << 7) | 0x7F
    // = 268435455 bytes (~256 MB)
    id3.append('\x7F');
    id3.append('\x7F');
    id3.append('\x7F');
    id3.append('\x7F');

    const QString path = createTempFile(id3);
    EmbeddedCover c = readEmbeddedCover(path);
    // Should detect that the declared size exceeds the actual file size
    CHECK(c.data.isEmpty());
    removeFile(path);
}

TEST_CASE("scanEmbeddedID3v2: syncsafe size of zero with non-empty body")
{
    // Size says 0 but there's actually data after the header
    // The scanner reads 0 bytes → should stop gracefully
    QByteArray id3;
    id3.append("ID3", 3);
    id3.append('\x04');
    id3.append('\x00');
    id3.append('\x00');
    id3.append('\x00');
    id3.append('\x00');
    id3.append('\x00');
    id3.append("extra_data_that_should_not_be_read");

    const QString path = createTempFile(id3);
    EmbeddedCover c = readEmbeddedCover(path);
    CHECK(c.data.isEmpty());
    removeFile(path);
}

TEST_CASE("scanEmbeddedID3v2: multiple ID3 candidates, first is false positive")
{
    // File with "ID3" text in data then a real ID3v2 header
    QByteArray data;
    data.append("This is not an ID3 tag but has ID3 in it\x00");
    // Now a valid ID3v2 header
    data.append("ID3", 3);
    data.append('\x04');
    data.append('\x00');
    data.append('\x00');
    data.append('\x00');
    data.append('\x00');
    data.append('\x01');  // size = 1 byte of body

    const QString path = createTempFile(data);
    EmbeddedCover c = readEmbeddedCover(path);
    // Should find the second "ID3" (the first one is in text, second triggers
    // a read that then fails the size check)
    CHECK(c.data.isEmpty());
    removeFile(path);
}

TEST_CASE("scanEmbeddedID3v2: unseekable-like file with large gap")
{
    // File with "ID3" 100 KB into the file — tests chunked scanning
    QByteArray data;
    data = data.leftJustified(100000, '\x00');
    data.append("ID3", 3);
    // Header at offset 100000
    data.append('\x04');
    data.append('\x00');
    data.append('\x00');
    data.append('\x00');
    data.append('\x00');
    data.append('\x00');  // size = 0

    const QString path = createTempFile(data);
    EmbeddedCover c = readEmbeddedCover(path);
    CHECK(c.data.isEmpty());
    removeFile(path);
}

TEST_CASE("scanEmbeddedID3v2: file full of ID3 magic bytes")
{
    // Every 4096 bytes there's an "ID3" — tests false-positive handling
    QByteArray data;
    data.append("ID3", 3);
    for (int i = 0; i < 100; ++i) {
        data.append(QByteArray(4093, 'A'));
        data.append("ID3", 3);
    }

    const QString path = createTempFile(data);
    EmbeddedCover c = readEmbeddedCover(path);
    CHECK(c.data.isEmpty());
    removeFile(path);
}

// ══════════════════════════════════════════════════════════════════════════════
// Format-specific corrupt files
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("readEmbeddedCover: corrupt MP3 (garbage)")
{
    QByteArray garbage;
    garbage.resize(10000);
    for (int i = 0; i < garbage.size(); ++i)
        garbage[i] = static_cast<char>((i * 37) & 0xFF);

    const QString path = createTempFile(garbage);
    // .mp3 extension triggers MPEG::File parser
    QString mp3Path = path + ".mp3";
    QFile::copy(path, mp3Path);

    EmbeddedCover c = readEmbeddedCover(mp3Path);
    CHECK(c.data.isEmpty());  // should not crash

    removeFile(path);
    removeFile(mp3Path);
}

TEST_CASE("readEmbeddedCover: corrupt FLAC")
{
    QByteArray flacHeader("fLaC\x00\x00\x00\x22", 8); // valid FLAC magic, invalid metadata
    flacHeader += QByteArray(1000, '\xFF');

    const QString path = createTempFile(flacHeader);
    QString flacPath = path + ".flac";
    QFile::copy(path, flacPath);

    EmbeddedCover c = readEmbeddedCover(flacPath);
    CHECK(c.data.isEmpty());

    removeFile(path);
    removeFile(flacPath);
}

TEST_CASE("readEmbeddedCover: corrupt Ogg (garbage instead of page)")
{
    QByteArray garbage(5000, '\x00');
    garbage[0] = 'O'; garbage[1] = 'g'; garbage[2] = 'g'; garbage[3] = 'S'; // wrong magic

    const QString path = createTempFile(garbage);
    QString oggPath = path + ".ogg";
    QFile::copy(path, oggPath);

    EmbeddedCover c = readEmbeddedCover(oggPath);
    CHECK(c.data.isEmpty());

    removeFile(path);
    removeFile(oggPath);
}

TEST_CASE("readEmbeddedCover: corrupt MP4 (no ftyp)")
{
    QByteArray data(100, '\x00');

    const QString path = createTempFile(data);
    QString m4aPath = path + ".m4a";
    QFile::copy(path, m4aPath);

    EmbeddedCover c = readEmbeddedCover(m4aPath);
    CHECK(c.data.isEmpty());

    removeFile(path);
    removeFile(m4aPath);
}

TEST_CASE("readEmbeddedCover: corrupt APE/WavPack")
{
    QByteArray data("wvpk", 4); // valid WavPack magic
    data += QByteArray(100, '\xFF');

    const QString path = createTempFile(data);
    QString wvPath = path + ".wv";
    QFile::copy(path, wvPath);

    EmbeddedCover c = readEmbeddedCover(wvPath);
    CHECK(c.data.isEmpty());

    removeFile(path);
    removeFile(wvPath);
}

TEST_CASE("readEmbeddedCover: corrupt Matroska (no EBML)")
{
    QByteArray data("matroska garbage", 16);

    const QString path = createTempFile(data);
    QString mkvPath = path + ".mkv";
    QFile::copy(path, mkvPath);

    EmbeddedCover c = readEmbeddedCover(mkvPath);
    CHECK(c.data.isEmpty());

    removeFile(path);
    removeFile(mkvPath);
}

TEST_CASE("readEmbeddedCover: Matroska with embedded ID3v2 in data section")
{
    // Create a file where "ID3" appears inside EBML data (not a valid ID3v2 tag)
    // The scanner should skip it since it won't match syncsafe
    QByteArray data;
    // Some Matroska-like data
    data.append('\x1A'); data.append('\x45'); data.append('\xDF'); data.append('\xA3'); // EBML header
    data.append(QByteArray(1000, '\x00'));
    // An "ID3" occurrence in the actual data area
    data.append("ID3", 3);
    data.append('\x04');
    data.append('\x00');
    // This one has valid syncsafe size but declares more data than available
    data.append('\x00');
    data.append('\x00');
    data.append('\x00');
    data.append('\x10');  // size = 16 bytes (but we only have a few left)

    const QString path = createTempFile(data);
    QString mkvPath = path + ".mkv";
    QFile::copy(path, mkvPath);

    EmbeddedCover c = readEmbeddedCover(mkvPath);
    CHECK(c.data.isEmpty());

    removeFile(path);
    removeFile(mkvPath);
}

TEST_CASE("readEmbeddedCover: unknown extension returns empty")
{
    QByteArray data(100, '\x00');
    const QString path = createTempFile(data);
    QString unknownPath = path + ".xyz";
    QFile::copy(path, unknownPath);

    EmbeddedCover c = readEmbeddedCover(unknownPath);
    CHECK(c.data.isEmpty());

    removeFile(path);
    removeFile(unknownPath);
}

TEST_CASE("readEmbeddedCover: file with no extension")
{
    QByteArray data(100, '\x00');
    const QString path = createTempFile(data);

    EmbeddedCover c = readEmbeddedCover(path);
    CHECK(c.data.isEmpty());

    removeFile(path);
}

TEST_CASE("readEmbeddedCover: empty path")
{
    EmbeddedCover c = readEmbeddedCover(QString());
    CHECK(c.data.isEmpty());
}

TEST_CASE("readEmbeddedCover: nonexistent path")
{
    EmbeddedCover c = readEmbeddedCover("/nonexistent/path/file.mp3");
    CHECK(c.data.isEmpty());
}

// ══════════════════════════════════════════════════════════════════════════════
// readEmbeddedLyrics — lyrics extraction stress
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("readEmbeddedLyrics: empty file")
{
    const QString path = createTempFile(QByteArray());
    QString lyrics = readEmbeddedLyrics(path);
    CHECK(lyrics.isEmpty());
    removeFile(path);
}

TEST_CASE("readEmbeddedLyrics: MP3 with corrupt ID3v2 USLT frame")
{
    // Build ID3v2 tag with USLT frame that has no text encoding byte
    QByteArray id3;
    id3.append("ID3", 3);
    id3.append('\x04');
    id3.append('\x00');

    QByteArray framePayload;
    // Skip text encoding byte — corrupt/malformed
    // Just put some text directly
    framePayload.append("Hello lyrics");

    quint32 frameSize = framePayload.size();
    QByteArray frameHeader;
    frameHeader.append("USLT");
    frameHeader.append(static_cast<char>((frameSize >> 24) & 0xFF));
    frameHeader.append(static_cast<char>((frameSize >> 16) & 0xFF));
    frameHeader.append(static_cast<char>((frameSize >> 8) & 0xFF));
    frameHeader.append(static_cast<char>(frameSize & 0xFF));
    frameHeader.append('\x00');
    frameHeader.append('\x00');

    QByteArray tagBody = frameHeader + framePayload;
    quint32 tagDataSize = tagBody.size();
    id3.append(static_cast<char>((tagDataSize >> 21) & 0x7F));
    id3.append(static_cast<char>((tagDataSize >> 14) & 0x7F));
    id3.append(static_cast<char>((tagDataSize >> 7) & 0x7F));
    id3.append(static_cast<char>(tagDataSize & 0x7F));
    id3.append(tagBody);

    const QString path = createTempFile(id3);
    QString mp3Path = path + ".mp3";
    QFile::copy(path, mp3Path);

    QString lyrics = readEmbeddedLyrics(mp3Path);
    // Should not crash; TagLib may or may not extract the corrupt frame
    // Either way, no crash is the test
    CHECK(true);
    removeFile(path);
    removeFile(mp3Path);
}

TEST_CASE("readEmbeddedLyrics: Matroska with corrupt embedded ID3v2")
{
    // MKV/MKA with a corrupt ID3v2 block that has valid syncsafe but
    // garbage inside — our scanner should find it, TagLib should reject it
    QByteArray data;
    // Add an "ID3" with valid syncsafe header but garbage body
    data.append("ID3", 3);
    data.append('\x04');
    data.append('\x00');
    // Size = 100 bytes of garbage
    data.append('\x00');
    data.append('\x00');
    data.append('\x00');
    data.append('\x64');  // syncsafe: 100
    data.append(QByteArray(100, '\xFF'));

    const QString path = createTempFile(data);
    QString mkaPath = path + ".mka";
    QFile::copy(path, mkaPath);

    QString lyrics = readEmbeddedLyrics(mkaPath);
    CHECK(lyrics.isEmpty());
    removeFile(path);
    removeFile(mkaPath);
}

// ══════════════════════════════════════════════════════════════════════════════
// CacheSqlite — SQL injection & stress tests
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("CacheSqlite: SQL injection via key — single quote")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    CacheSqlite cache(tmp.filePath("cache.db"));

    QString evilKey = QStringLiteral("cover:O'Brian:Album");
    QByteArray value("test data");
    CHECK(cache.put(evilKey, value));

    QByteArray retrieved = cache.get(evilKey);
    CHECK_EQ(retrieved, value);
}

TEST_CASE("CacheSqlite: SQL injection via key — semicolon")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    CacheSqlite cache(tmp.filePath("cache2.db"));

    QString evilKey = QStringLiteral("key; DROP TABLE cache; --");
    QByteArray value("test");
    CHECK(cache.put(evilKey, value));

    QByteArray retrieved = cache.get(evilKey);
    CHECK_EQ(retrieved, value);
}

TEST_CASE("CacheSqlite: SQL injection via key — UNION injection")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    CacheSqlite cache(tmp.filePath("cache3.db"));

    QString evilKey = QStringLiteral("' UNION SELECT * FROM cache --");
    QByteArray value("sneaky");
    CHECK(cache.put(evilKey, value));

    QByteArray retrieved = cache.get(evilKey);
    CHECK_EQ(retrieved, value);
}

TEST_CASE("CacheSqlite: SQL injection via key — comment injection")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    CacheSqlite cache(tmp.filePath("cache4.db"));

    // Double-dash comment attempt
    QString evilKey = QStringLiteral("valid_key -- comment");
    QByteArray value("data");
    CHECK(cache.put(evilKey, value));
    CHECK_EQ(cache.get(evilKey), value);

    // Block comment attempt
    QString evilKey2 = QStringLiteral("valid/*comment*/key");
    CHECK(cache.put(evilKey2, QByteArray("data2")));
    CHECK_EQ(cache.get(evilKey2), QByteArray("data2"));
    CHECK_EQ(cache.get(evilKey), value);  // other key still intact
}

TEST_CASE("CacheSqlite: SQL injection via value — binary blob")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    CacheSqlite cache(tmp.filePath("cache5.db"));

    // Value containing SQL-injection-like binary
    QByteArray evilValue;
    evilValue.append("'); DROP TABLE cache; --");
    evilValue.append('\x00');
    evilValue.append(QByteArray("'; DELETE FROM cache; --"));

    QString key = "test_key";
    CHECK(cache.put(key, evilValue));
    CHECK_EQ(cache.get(key), evilValue);
}

TEST_CASE("CacheSqlite: extremely long key")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    CacheSqlite cache(tmp.filePath("cache6.db"));

    QString longKey(10000, 'A');
    QByteArray value("data");
    CHECK(cache.put(longKey, value));
    CHECK_EQ(cache.get(longKey), value);
}

TEST_CASE("CacheSqlite: extremely large value")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    CacheSqlite cache(tmp.filePath("cache7.db"));

    QByteArray hugeValue(500000, '\xFF');
    QString key = "huge";
    CHECK(cache.put(key, hugeValue));
    CHECK_EQ(cache.get(key), hugeValue);
}

TEST_CASE("CacheSqlite: unicode keys")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    CacheSqlite cache(tmp.filePath("cache8.db"));

    QString unicodeKey = QStringLiteral("ключ:исполнитель:альбом");
    QByteArray value("cover data");
    CHECK(cache.put(unicodeKey, value));
    CHECK_EQ(cache.get(unicodeKey), value);
}

TEST_CASE("CacheSqlite: empty key is allowed by SQLite")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    CacheSqlite cache(tmp.filePath("cache9.db"));

    // SQLite allows empty text keys — put should succeed
    CHECK(cache.put(QString(), QByteArray("data")));
    // But get with empty string should retrieve it
    CHECK_EQ(cache.get(QString()), QByteArray("data"));
}

TEST_CASE("CacheSqlite: empty value")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    CacheSqlite cache(tmp.filePath("cache10.db"));

    CHECK(cache.put("empty_val", QByteArray()));
    QByteArray ret = cache.get("empty_val");
    CHECK(ret.isEmpty());
}

TEST_CASE("CacheSqlite: remove with injection key")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    CacheSqlite cache(tmp.filePath("cache11.db"));

    QString evilKey = QStringLiteral("'; DELETE FROM cache; --");
    QByteArray value("to_delete");
    cache.put(evilKey, value);
    CHECK(cache.remove(evilKey));

    // Verify other data is intact
    cache.put("normal", QByteArray("ok"));
    CHECK_EQ(cache.get("normal"), QByteArray("ok"));
    CHECK(cache.get(evilKey).isEmpty());  // was removed
}

TEST_CASE("CacheSqlite: put/get stress across many keys")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    CacheSqlite cache(tmp.filePath("cache_stress.db"));

    // Put 1000 keys
    for (int i = 0; i < 1000; ++i) {
        QString key = QStringLiteral("key%1").arg(i);
        QByteArray val = QByteArray::number(i);
        CHECK(cache.put(key, val));
    }

    // Read them all back
    for (int i = 0; i < 1000; ++i) {
        QString key = QStringLiteral("key%1").arg(i);
        QByteArray expected = QByteArray::number(i);
        CHECK_EQ(cache.get(key), expected);
    }
}
