#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFileInfo>

#include "metadata/cache-sqlite.h"

TEST_CASE("CacheSqlite put and get")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString dbPath = tmp.filePath("test.db");

    CacheSqlite cache(dbPath);
    CHECK(cache.put("key1", QByteArray("hello")));
    CHECK(cache.put("key2", QByteArray("world")));

    CHECK(cache.get("key1") == QByteArray("hello"));
    CHECK(cache.get("key2") == QByteArray("world"));
}

TEST_CASE("CacheSqlite get nonexistent returns empty")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    CacheSqlite cache(tmp.filePath("empty.db"));
    CHECK(cache.get("nonexistent").isEmpty());
}

TEST_CASE("CacheSqlite upsert replaces existing value")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    CacheSqlite cache(tmp.filePath("upsert.db"));

    cache.put("k", QByteArray("old"));
    cache.put("k", QByteArray("new"));
    CHECK(cache.get("k") == QByteArray("new"));
}

TEST_CASE("CacheSqlite remove")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    CacheSqlite cache(tmp.filePath("remove.db"));

    cache.put("k", QByteArray("data"));
    CHECK(cache.get("k") == QByteArray("data"));
    CHECK(cache.remove("k"));
    CHECK(cache.get("k").isEmpty());
}

TEST_CASE("CacheSqlite cleanup does not crash on empty db")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    CacheSqlite cache(tmp.filePath("cleanup.db"));
    cache.put("a", QByteArray("1"));
    cache.cleanup();
    CHECK(cache.get("a") == QByteArray("1"));
}

TEST_CASE("CacheSqlite binary data roundtrip")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    CacheSqlite cache(tmp.filePath("binary.db"));

    QByteArray data;
    data.resize(256);
    for (int i = 0; i < 256; ++i)
        data[i] = static_cast<char>(i);

    cache.put("bin", data);
    QByteArray result = cache.get("bin");
    CHECK(result == data);
    CHECK(result.size() == 256);
}
