#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-song-info-dialog.cpp
 * @brief Тесты окна «Song Info» и поиска тегов трэка по URI.
 *
 * Менеджер метаданных не поднимается: без него диалог живёт на одних тегах, а приход
 * лирики подаётся прямо в приватный слот. Живой кейс включается GMPC_QT_TEST_MPD и
 * проверяет `MpdCommands::songForUri` — единственный путь получить теги строки.
 */

#include <QApplication>
#include <QLabel>
#include <QPlainTextEdit>

#include "core/connection.h"
#include "core/mpd-commands.h"
#include "core/mpd-endpoint.h"
#include "gui/song-info-dialog.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QApplication app(argc, &argv);

namespace {

QLabel *label(SongInfoDialog &dlg, const char *name)
{
    return dlg.findChild<QLabel *>(QString::fromLatin1(name));
}

void feedLyrics(SongInfoDialog &dlg, const QString &artist, const QString &title,
                const QString &text)
{
    QMetaObject::invokeMethod(&dlg, "onLyricsReady", Qt::DirectConnection,
                              Q_ARG(QString, artist), Q_ARG(QString, title),
                              Q_ARG(QString, text));
}

SongEntry makeSong(const char *artist, const char *title, const char *album)
{
    SongEntry s;
    s.artist = QString::fromUtf8(artist);
    s.title  = QString::fromUtf8(title);
    s.album  = QString::fromUtf8(album);
    return s;
}

MpdEndpoint endpointFromEnv()
{
    MpdEndpoint e;
    e.host = qEnvironmentVariable("GMPC_QT_TEST_MPD");
    e.timeoutMs = 5000;
    return e;
}

} // namespace

TEST_CASE("окно Song Info показывает теги выбранного трэка")
{
    const SongEntry song = makeSong("Artist A", "Title T", "Album X");
    SongInfoDialog dlg(song);

    CHECK_EQ(dlg.windowTitle(), QStringLiteral("Song Info"));
    CHECK_EQ(label(dlg, "title")->text(), QStringLiteral("Title T"));
    CHECK_EQ(label(dlg, "artist")->text(), QStringLiteral("Artist A"));
    CHECK_EQ(label(dlg, "album")->text(), QStringLiteral("Album X"));
}

TEST_CASE("без тега Title заголовком становится имя файла")
{
    SongEntry song = makeSong("A", "", "");
    song.uri = QStringLiteral("dir/deep track.flac");
    SongInfoDialog dlg(song);

    CHECK_EQ(label(dlg, "title")->text(), QStringLiteral("deep track.flac"));
}

TEST_CASE("длительность форматируется, а неизвестная помечается словом")
{
    SongEntry song = makeSong("A", "T", "Al");
    song.duration = 125;
    {
        SongInfoDialog dlg(song);
        CHECK_EQ(label(dlg, "length")->text(), QStringLiteral("2:05"));
    }
    song.duration = 0;
    SongInfoDialog quiet(song);
    // «0:00» соврало бы про неизвестную длительность.
    CHECK_FALSE(quiet.findChild<QLabel *>(QStringLiteral("length"))
                    ->text()
                    .contains(QStringLiteral(":")));
}

TEST_CASE("без менеджера лирика помечается отсутствующей, сеть не дёргается")
{
    const SongEntry song = makeSong("A", "T", "Al");
    SongInfoDialog dlg(song);

    CHECK_FALSE(label(dlg, "hintLabel")->text().isEmpty());
    auto *lyrics = dlg.findChild<QPlainTextEdit *>(QStringLiteral("lyricsView"));
    REQUIRE(lyrics != nullptr);
    CHECK(lyrics->toPlainText().isEmpty());
}

TEST_CASE("лирика текущего трэка попадает в окно, чужой ответ отброшен")
{
    const SongEntry song = makeSong("Artist A", "Title T", "Album X");
    SongInfoDialog dlg(song);

    feedLyrics(dlg, QStringLiteral("Artist A"), QStringLiteral("Title T"),
               QStringLiteral("строка один\nстрока два"));
    auto *lyrics = dlg.findChild<QPlainTextEdit *>(QStringLiteral("lyricsView"));
    REQUIRE(lyrics != nullptr);
    CHECK_EQ(lyrics->toPlainText(), QStringLiteral("строка один\nстрока два"));
    CHECK(label(dlg, "hintLabel")->text().isEmpty());

    // Окно открыто на другом трэке — ответ по прошлому выбору переписывать не должен.
    feedLyrics(dlg, QStringLiteral("Other"), QStringLiteral("Song"),
               QStringLiteral("чужая"));
    CHECK_EQ(lyrics->toPlainText(), QStringLiteral("строка один\nстрока два"));
}

TEST_CASE("отсутствие лирики у провайдеров показывается словами")
{
    const SongEntry song = makeSong("A", "T", "Al");
    SongInfoDialog dlg(song);

    QMetaObject::invokeMethod(&dlg, "onLyricsNotFound", Qt::DirectConnection,
                              Q_ARG(QString, QStringLiteral("A")),
                              Q_ARG(QString, QStringLiteral("T")));
    CHECK(label(dlg, "hintLabel")->text().contains(QStringLiteral("lyrics"),
                                                   Qt::CaseInsensitive));
}

// ── Живой кейс: теги трэка по URI берутся у сервера (иначе их взять негде) ───

TEST_CASE("songForUri отдаёт теги файла из базы, а чужой путь — nullopt")
{
    const MpdEndpoint endpoint = endpointFromEnv();
    if (!endpoint.isValid()) {
        MESSAGE("GMPC_QT_TEST_MPD не задан — интеграционная проверка пропущена");
        return;
    }

    Connection conn;
    REQUIRE(conn.connectToServer(endpoint.host, endpoint.port, endpoint.password,
                                 endpoint.timeoutMs));
    MpdCommands cmds(&conn);

    // Одноразовый сервер держит a1.flac с тегами Artist One / Track 1.
    const auto found = cmds.songForUri(QStringLiteral("a1.flac"));
    REQUIRE(found.has_value());
    CHECK_EQ(found->artist, QStringLiteral("Artist One"));

    // Точное совпадение URI: подстроки и регистр не годятся (замер 10).
    CHECK_FALSE(cmds.songForUri(QStringLiteral("a1")).has_value());

    conn.disconnect();
}