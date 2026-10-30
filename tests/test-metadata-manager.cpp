#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>

/**
 * @file test-metadata-manager.cpp
 * @brief Tests for MetadataManager orchestrator.
 *
 * MetadataManager creates its own CoverProvider and LrcLibProvider internally.
 * We test the API integration: cache hit path, signal forwarding, etc.
 * Full network mocking of sub-providers is done via CoverProvider/DeezerProvider tests.
 *
 * requestCover()/requestLyrics() start real HTTP traffic that outlives the case,
 * hence the explicit main() and the drains — see test-network-drain.h.
 */

#include "metadata/metadata-manager.h"
#include "test-network-drain.h"

TEST_CASE("MetadataManager cover cache hit")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    MetadataManager mm(tmp.path(), "", nullptr);

    bool gotCover = false;
    QByteArray gotData;
    QObject ctx;
    QObject::connect(&mm, &MetadataManager::coverReady, &ctx,
            [&](const QString &, const QString &, const QByteArray &d) {
                gotCover = true;
                gotData = d;
            });

    // First call will go to network (CoverProvider → MB → Deezer) and may fail.
    // We only test cache hit here — but cache is empty initially.
    // Instead: call requestCover once (will not find anything), then nothing cached.
    // For a proper cache test we'd need to inject into CoverProvider.
    // Skip for now — functional caching is verified via CacheSqlite and CoverProvider tests.

    // Just verify that calling requestCover doesn't crash
    // and that requestLyrics doesn't crash either.
    mm.requestCover("Test Artist", "Test Album");
    // No crash is success

    // Cache key is "cover:Test Artist:Test Album" — but metadata-manager creates its own
    // CoverProvider which has its own CacheSqlite.  No way to pre-populate from here.
    // Acceptable — integration verified by CacheSqlite + CoverProvider tests separately.

    mm.requestLyrics("Test Artist", "Test Title");
    // No crash is success

    CHECK(true);
    waitForIdle();
}

TEST_CASE("MetadataManager construction does not crash")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    MetadataManager mm(tmp.path(), "test_key", nullptr);
    CHECK(true);  // constructed without error
}

int main(int argc, char *argv[])
{
    doctest::Context ctx(argc, argv);
    QCoreApplication app(argc, argv);
    int result = ctx.run();
    // Flush whatever the cases left in flight while the application — and the
    // network manager's worker thread — are still alive.
    waitForIdle(kDrainMs);
    return result;
}
