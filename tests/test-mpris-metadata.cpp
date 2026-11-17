#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-mpris-metadata.cpp
 * @brief Поля MPRIS `Metadata`, собранные из тегов трэка.
 *
 * DBus и соединение здесь не нужны: сбока карты — чистая функция, а сам
 * адаптор только подаёт в неё кэш. Поэтому сьют живёт без сервера вовсе.
 */

#include <QApplication>
#include <QVariantMap>

#include "mpris/metadata-map.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QApplication app(argc, &argv);

TEST_CASE("полные теги попадают в карту под ключами mpris:")
{
    const QVariantMap m = mprisTextMetadata(QStringLiteral("Song"),
                                            QStringLiteral("Artist"),
                                            QStringLiteral("Album"), 0);
    CHECK(m.value("mpris:title").toString() == "Song");
    CHECK(m.value("mpris:artist").toString() == "Artist");
    CHECK(m.value("mpris:album").toString() == "Album");
}

TEST_CASE("отсутствующий тег не попадает в карту вовсе")
{
    // Пустая строка — это «поля нет», а не пустое значение: клиент различает их,
    // и подставлять пустоту вместо отсутствия значило бы врать.
    const QVariantMap m = mprisTextMetadata(QStringLiteral("Song"), {}, {}, 0);
    CHECK(m.value("mpris:title").toString() == "Song");
    CHECK(m.value("mpris:artist").isNull());
    CHECK(m.value("mpris:album").isNull());
}

TEST_CASE("длительность уходит в микросекундах, неизвестная — никуда")
{
    const QVariantMap known = mprisTextMetadata({}, {}, {},
                                                static_cast<double>(125.5));
    CHECK_EQ(known.value("mpris:length").toLongLong(), 125500000);

    // Ноль и отрицательное значение означают «длительность неизвестна» — ключа
    // быть не должно, иначе клиент покажет трэк нулевой длительности.
    const QVariantMap unknown = mprisTextMetadata({}, {}, {},
                                                  static_cast<double>(0));
    CHECK(unknown.value("mpris:length").isNull());
}

TEST_CASE("пустые теги не оставляют ни одного ключа")
{
    const QVariantMap m = mprisTextMetadata({}, {}, {}, 0);
    CHECK(m.value("mpris:title").isNull());
    CHECK(m.value("mpris:artist").isNull());
    CHECK(m.value("mpris:album").isNull());
    CHECK(m.value("mpris:length").isNull());
}