#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-song-info.cpp
 * @brief Tests for SongInfo.
 */

#include <QApplication>
#include <QLabel>

#include "widgets/song-info.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QApplication app(argc, &argv);

TEST_CASE("SongInfo has three labels")
{
    SongInfo info;
    auto labels = info.findChildren<QLabel *>();
    CHECK_EQ(labels.size(), 3);
}

TEST_CASE("SongInfo showSong populates labels") {
    SongInfo info;
    SongEntry song;
    song.artist = "Test Artist";
    song.title = "Test Title";
    song.album = "Test Album";

    info.showSong(&song);

    auto labels = info.findChildren<QLabel *>();
    // find the ones with text
    int textCount = 0;
    for (auto *l : labels) {
        if (!l->text().isEmpty())
            ++textCount;
    }
    CHECK_EQ(textCount, 3);
}

TEST_CASE("SongInfo showSong with nullptr clears")
{
    SongInfo info;
    SongEntry song;
    song.artist = "A";
    song.title = "T";
    song.album = "Al";
    info.showSong(&song);

    info.showSong(nullptr);

    auto labels = info.findChildren<QLabel *>();
    for (auto *l : labels)
        CHECK(l->text().isEmpty());
}

TEST_CASE("SongInfo clear empties all labels")
{
    SongInfo info;
    SongEntry song;
    song.artist = "A";
    song.title = "T";
    song.album = "Al";
    info.showSong(&song);

    info.clear();

    auto labels = info.findChildren<QLabel *>();
    for (auto *l : labels)
        CHECK(l->text().isEmpty());
}

TEST_CASE("SongInfo initially empty")
{
    SongInfo info;
    auto labels = info.findChildren<QLabel *>();
    for (auto *l : labels)
        CHECK(l->text().isEmpty());
}
