#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QObject>
#include <QUrl>

/**
 * @file test-deezer-provider.cpp
 * @brief Tests for DeezerProvider — construction and non-crash verification.
 *
 * AsyncDownload::get() is not virtual, so we cannot mock it via subclassing.
 * These tests verify that the API contract doesn't crash on construction/fetch.
 * Full pipeline coverage is verified via CacheSqlite + CoverWidget + integration tests.
 *
 * fetchCover() leaves a reply in flight when the case returns, hence the
 * explicit main() and the drains — see test-network-drain.h.
 */

#include "metadata/cover-provider.h"
#include "test-network-drain.h"

TEST_CASE("DeezerProvider construction and fetchCover does not crash")
{
    DeezerProvider prov;
    bool called = false;
    QObject ctx;
    QObject::connect(&prov, &DeezerProvider::notFound, &ctx, [&]() { called = true; });
    QObject::connect(&prov, &DeezerProvider::error, &ctx, [&](const QString &) { called = true; });
    // May hit real network — test only checks no crash
    prov.fetchCover("__gmpc_test_nonexistent__", "__gmpc_test_nonexistent__");
    CHECK(true);
    waitForIdle();
}

TEST_CASE("DeezerProvider multiple fetchCover calls don't crash")
{
    DeezerProvider prov;
    prov.fetchCover("A", "B");
    prov.fetchCover("C", "D");
    CHECK(true);
    waitForIdle();
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
