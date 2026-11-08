#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-now-playing.cpp
 * @brief Tests for the Now Playing page.
 *
 * Менеджер метаданных не поднимаем: без него страница показывает только теги, а
 * приход лирики подаётся прямо в приватный слот — так ни одного сетевого запроса
 * и никакой зависимости от таймингов провайдеров.
 */

#include <QApplication>
#include <QLabel>
#include <QPlainTextEdit>

#include "browsers/now-playing.h"
#include "core/playlist-model.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QApplication app(argc, &argv);

namespace {

QLabel *label(NowPlaying &page, const char *name)
{
    return page.findChild<QLabel *>(QString::fromLatin1(name));
}

/// Подкладывает ответ провайдера так, как его прислал бы MetadataManager.
void feedLyrics(NowPlaying &page, const QString &artist, const QString &title,
                const QString &text)
{
    QMetaObject::invokeMethod(&page, "onLyricsReady", Qt::DirectConnection,
                              Q_ARG(QString, artist), Q_ARG(QString, title),
                              Q_ARG(QString, text));
}

void feedNoLyrics(NowPlaying &page, const QString &artist, const QString &title)
{
    QMetaObject::invokeMethod(&page, "onLyricsNotFound", Qt::DirectConnection,
                              Q_ARG(QString, artist), Q_ARG(QString, title));
}

SongEntry makeSong(const char *artist, const char *title, const char *album)
{
    SongEntry s;
    s.artist = QString::fromUtf8(artist);
    s.title  = QString::fromUtf8(title);
    s.album  = QString::fromUtf8(album);
    return s;
}

} // namespace

TEST_CASE("страница Now Playing пуста и сообщает, что ничего не играет")
{
    NowPlaying page;
    CHECK_EQ(page.title(), QStringLiteral("Now Playing"));

    auto *hint = label(page, "hintLabel");
    REQUIRE(hint != nullptr);
    CHECK_FALSE(hint->text().isEmpty());
    // Заголовок пуст: сообщение о простое живёт в строке состояния.
    CHECK(label(page, "titleLabel")->text().isEmpty());

    auto *lyrics = page.findChild<QPlainTextEdit *>(QStringLiteral("lyricsView"));
    REQUIRE(lyrics != nullptr);
    CHECK(lyrics->toPlainText().isEmpty());
}

TEST_CASE("setSong заполняет теги, а без менеджера лирика помечается отсутствующей")
{
    NowPlaying page;
    const SongEntry song = makeSong("Artist A", "Title T", "Album X");
    page.setSong(&song);

    CHECK_EQ(label(page, "titleLabel")->text(), QStringLiteral("Title T"));
    CHECK_EQ(label(page, "artistLabel")->text(), QStringLiteral("Artist A"));
    CHECK_EQ(label(page, "albumLabel")->text(), QStringLiteral("Album X"));

    // Менеджера нет — сеть не дёргается вовсе.
    CHECK_FALSE(label(page, "hintLabel")->text().isEmpty());
}

TEST_CASE("без тега Title заголовком становится имя файла")
{
    NowPlaying page;
    SongEntry song = makeSong("Artist", "", "");
    song.uri = QStringLiteral("dir/sub/deep track.flac");
    page.setSong(&song);

    CHECK_EQ(label(page, "titleLabel")->text(), QStringLiteral("deep track.flac"));
}

TEST_CASE("лирика текущего трэка попадает в вид, чужой ответ отброшен")
{
    NowPlaying page;
    const SongEntry song = makeSong("Artist A", "Title T", "Album X");
    page.setSong(&song);

    feedLyrics(page, QStringLiteral("Artist A"), QStringLiteral("Title T"),
               QStringLiteral("первая строка\nвторая"));
    auto *lyrics = page.findChild<QPlainTextEdit *>(QStringLiteral("lyricsView"));
    REQUIRE(lyrics != nullptr);
    CHECK_EQ(lyrics->toPlainText(), QStringLiteral("первая строка\nвторая"));
    CHECK(label(page, "hintLabel")->text().isEmpty());

    // Ответ по трэку, который уже сменился: экран переписывать не должен.
    feedLyrics(page, QStringLiteral("Other"), QStringLiteral("Song"),
               QStringLiteral("чужая лирика"));
    CHECK_EQ(lyrics->toPlainText(), QStringLiteral("первая строка\nвторая"));
}

TEST_CASE("смена трэка сбрасывает прежний текст песни")
{
    NowPlaying page;
    const SongEntry first = makeSong("A", "One", "Al");
    page.setSong(&first);
    feedLyrics(page, QStringLiteral("A"), QStringLiteral("One"),
               QStringLiteral("текст первой"));

    auto *lyrics = page.findChild<QPlainTextEdit *>(QStringLiteral("lyricsView"));
    REQUIRE(lyrics != nullptr);
    CHECK_FALSE(lyrics->toPlainText().isEmpty());

    const SongEntry second = makeSong("B", "Two", "Al2");
    page.setSong(&second);
    // Чужой текст не должен пережить смену трэка.
    CHECK(lyrics->toPlainText().isEmpty());
}

TEST_CASE("отсутствие лирики у провайдеров показывается словами")
{
    NowPlaying page;
    const SongEntry song = makeSong("A", "One", "Al");
    page.setSong(&song);

    feedNoLyrics(page, QStringLiteral("A"), QStringLiteral("One"));
    CHECK(label(page, "hintLabel")->text().contains(QStringLiteral("lyrics"),
                                                    Qt::CaseInsensitive));
}

TEST_CASE("setSong(nullptr) и отключение очищают экран")
{
    NowPlaying page;
    const SongEntry song = makeSong("A", "One", "Al");
    page.setSong(&song);
    feedLyrics(page, QStringLiteral("A"), QStringLiteral("One"),
               QStringLiteral("текст"));

    // Поздний ответ после «ничего не играет» уже не должен попадать на экран.
    page.onConnectionChanged(false);
    CHECK(label(page, "hintLabel")->text().contains(QStringLiteral("Nothing")));

    auto *lyrics = page.findChild<QPlainTextEdit *>(QStringLiteral("lyricsView"));
    REQUIRE(lyrics != nullptr);
    feedLyrics(page, QStringLiteral("A"), QStringLiteral("One"),
               QStringLiteral("опоздавший текст"));
    CHECK(lyrics->toPlainText().isEmpty());

    page.setSong(nullptr);
    CHECK(label(page, "artistLabel")->text().isEmpty());
}