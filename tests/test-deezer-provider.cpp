#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-deezer-provider.cpp
 * @brief DeezerProvider tests — seam-based, no network.
 *
 * Feeds crafted JSON into onSearchReply / onImageReply through
 * the testInject*() seams.  The heavy fuzz coverage lives in
 * test-provider-parsers.cpp; this file keeps focused cases that
 * verify the basic signal contracts.
 */

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "metadata/cover-provider.h"

TEST_CASE("Deezer: image found → coverFound fires")
{
    DeezerProvider prov;
    bool gotCover = false;
    QObject ctx;
    QObject::connect(&prov, &DeezerProvider::coverFound, &ctx,
                     [&](const QByteArray &) { gotCover = true; });

    prov.testInjectImageReply(true, QByteArray("jpeg_data"));
    CHECK(gotCover);
}

TEST_CASE("Deezer: image empty → error fires")
{
    DeezerProvider prov;
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &DeezerProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    prov.testInjectImageReply(true, QByteArray());
    CHECK(errorMsg.contains("failed", Qt::CaseInsensitive));
}

TEST_CASE("Deezer: image network error → error fires")
{
    DeezerProvider prov;
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &DeezerProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    prov.testInjectImageReply(false, QByteArray("data"));
    CHECK(errorMsg.contains("failed", Qt::CaseInsensitive));
}

TEST_CASE("Deezer: search empty data → notFound")
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

TEST_CASE("Deezer: search missing data → notFound")
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

TEST_CASE("Deezer: search with valid result triggers image download (no crash)")
{
    DeezerProvider prov;
    QJsonObject artist; artist["name"] = "Test";
    QJsonObject item; item["artist"] = artist; item["cover_big"] = "https://example.com/c.jpg";
    QJsonArray data; data.append(item);
    QJsonObject root; root["data"] = data;
    // Создаёт dl_->get() на реальный URL — сигнал не придёт, но краша быть не должно.
    prov.testInjectSearchReply(true, QJsonDocument(root).toJson());
    CHECK(true);
}
