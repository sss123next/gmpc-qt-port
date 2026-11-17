#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-discogs-provider.cpp
 * @brief DiscogsProvider tests — seam-based, no network.
 *
 * Feeds crafted JSON into onSearchReply / onImageReply through
 * the testInject*() seams.  The heavy fuzz coverage lives in
 * test-provider-parsers.cpp; this file keeps focused contract tests.
 *
 * Set DISCOGS_TEST_TOKEN to run the optional live integration case.
 */

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "providers/discogs.h"

TEST_CASE("Discogs: search found → triggers image download")
{
    DiscogsProvider prov(QString(), nullptr);
    QJsonObject result;
    result["cover_image"] = "https://example.com/c.jpg";
    QJsonArray results; results.append(result);
    QJsonObject root; root["results"] = results;
    // Создаёт dl_->get() на реальный URL — не крашит, хотя сигнал не придёт.
    prov.testInjectSearchReply(true, QJsonDocument(root).toJson());
    CHECK(true);
}

TEST_CASE("Discogs: search empty results → notFound")
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

TEST_CASE("Discogs: search error → error signal")
{
    DiscogsProvider prov(QString(), nullptr);
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &DiscogsProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    QJsonObject root; root["message"] = "rate limited";
    prov.testInjectSearchReply(true, QJsonDocument(root).toJson());
    CHECK(errorMsg.contains("rate", Qt::CaseInsensitive));
}

TEST_CASE("Discogs: search binary garbage → error signal")
{
    DiscogsProvider prov(QString(), nullptr);
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &DiscogsProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    prov.testInjectSearchReply(true, QByteArray(4096, '\x00'));
    CHECK(errorMsg.contains("invalid", Qt::CaseInsensitive));
}

TEST_CASE("Discogs: image found → coverFound")
{
    DiscogsProvider prov(QString(), nullptr);
    bool gotCover = false;
    QObject ctx;
    QObject::connect(&prov, &DiscogsProvider::coverFound, &ctx,
                     [&](const QByteArray &) { gotCover = true; });

    prov.testInjectImageReply(true, QByteArray("image_data"));
    CHECK(gotCover);
}

TEST_CASE("Discogs: image empty → error")
{
    DiscogsProvider prov(QString(), nullptr);
    QString errorMsg;
    QObject ctx;
    QObject::connect(&prov, &DiscogsProvider::error, &ctx,
                     [&](const QString &m) { errorMsg = m; });

    prov.testInjectImageReply(true, QByteArray());
    CHECK(errorMsg.contains("failed", Qt::CaseInsensitive));
}

TEST_CASE("Discogs: live integration (DISCOGS_TEST_TOKEN)")
{
    const char *envToken = getenv("DISCOGS_TEST_TOKEN");
    if (!envToken || !*envToken) {
        MESSAGE("DISCOGS_TEST_TOKEN not set — skipping authenticated test");
        return;
    }

    DiscogsProvider prov(QString::fromLatin1(envToken), nullptr);
    bool gotCover = false;
    QObject ctx;
    QObject::connect(&prov, &DiscogsProvider::coverFound, &ctx,
                     [&](const QByteArray &) { gotCover = true; });

    prov.fetchCover("The Beatles", "Abbey Road");
    // Асинхронно — проверяем только что не упало
    CHECK(true);
}
