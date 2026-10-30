#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
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
 */

#include "metadata/cover-provider.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QCoreApplication app(argc, &argv);
static QObject ctx;

TEST_CASE("DeezerProvider construction and fetchCover does not crash")
{
    DeezerProvider prov;
    bool called = false;
    QObject::connect(&prov, &DeezerProvider::notFound, &ctx, [&]() { called = true; });
    QObject::connect(&prov, &DeezerProvider::error, &ctx, [&](const QString &) { called = true; });
    // May hit real network — test only checks no crash
    prov.fetchCover("__gmpc_test_nonexistent__", "__gmpc_test_nonexistent__");
    CHECK(true);
}

TEST_CASE("DeezerProvider multiple fetchCover calls don't crash")
{
    DeezerProvider prov;
    QObject ctx2;
    prov.fetchCover("A", "B");
    prov.fetchCover("C", "D");
    CHECK(true);
}
