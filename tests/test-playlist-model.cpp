#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-playlist-model.cpp
 * @brief Tests for PlaylistModel (QAbstractListModel for MPD queue).
 *
 * Requires QApplication (offscreen) for Qt model/view machinery.
 */

#include <QApplication>
#include <QModelIndex>

#include "core/playlist-model.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QApplication app(argc, &argv);

TEST_CASE("PlaylistModel initial state is empty")
{
    PlaylistModel model;
    CHECK_EQ(model.rowCount({}), 0);
}

TEST_CASE("PlaylistModel roles contain expected keys")
{
    PlaylistModel model;

    // Even with 0 rows we can check roleNames()
    auto roles = model.roleNames();
    CHECK_FALSE(roles.isEmpty());

    // Our Roles enum has Title = Qt::UserRole + 1, Artist, Album, etc.
    CHECK(roles.contains(PlaylistModel::TitleRole));
    CHECK(roles.contains(PlaylistModel::ArtistRole));
    CHECK(roles.contains(PlaylistModel::AlbumRole));
    CHECK(roles.contains(PlaylistModel::DurationRole));
}

TEST_CASE("PlaylistModel data() returns empty for out-of-range index")
{
    PlaylistModel model;
    QModelIndex idx = model.index(0, 0);
    CHECK_FALSE(idx.isValid());
    CHECK(model.data(idx, Qt::DisplayRole).isValid() == false);
}

TEST_CASE("PlaylistModel can be cleared safely before any data loaded")
{
    PlaylistModel model;
    // Calling beginResetModel / endResetModel internal — but we can
    // check that rowCount is 0 and clear doesn't crash.
    model.clear();
    CHECK_EQ(model.rowCount({}), 0);
}

TEST_CASE("PlaylistModel emits modelReset on clear")
{
    PlaylistModel model;
    bool resetEmitted = false;
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&] {
        resetEmitted = true;
    });
    model.clear();
    CHECK(resetEmitted);
}

TEST_CASE("headerData returns column name for known columns")
{
    PlaylistModel model;
    CHECK_FALSE(model.headerData(0, Qt::Horizontal).toString().isEmpty());
}

// ── Current song (source of truth is MPD status) ────────────────────────────

namespace {

SongEntry makeSong(int id, int pos, const QString &title)
{
    SongEntry e;
    e.id = id;
    e.pos = pos;
    e.artist = QStringLiteral("artist");
    e.title = title;
    e.album = QStringLiteral("album");
    e.duration = 100 + pos;
    return e;
}

QList<SongEntry> threeSongs()
{
    return { makeSong(101, 0, "first"),
             makeSong(102, 1, "second"),
             makeSong(103, 2, "third") };
}

/// Collects every id passed to currentSongChanged().
class SongSignals {
public:
    explicit SongSignals(PlaylistModel &model)
    {
        QObject::connect(&model, &PlaylistModel::currentSongChanged,
                         [this](int id) { ids.append(id); });
    }
    QList<int> ids;
};

} // namespace

TEST_CASE("no song is current until MPD reports one")
{
    PlaylistModel model;
    model.setSongs(threeSongs());
    // Regression: the model used to hand out songs_[0] as "current".
    CHECK(model.currentSong() == nullptr);
    CHECK_EQ(model.currentSongId(), -1);
}

TEST_CASE("the current song is the one status names, not row zero")
{
    PlaylistModel model;
    model.setSongs(threeSongs());
    model.setCurrentSong(102, 1);

    REQUIRE(model.currentSong() != nullptr);
    CHECK_EQ(model.currentSong()->id, 102);
    CHECK_EQ(model.currentSong()->title, QStringLiteral("second"));
}

TEST_CASE("the song id wins over a stale queue position")
{
    PlaylistModel model;
    model.setSongs(threeSongs());
    // MPD can report a position that no longer matches after queue edits; the
    // id is stable, so it must decide.
    model.setCurrentSong(103, 0);

    REQUIRE(model.currentSong() != nullptr);
    CHECK_EQ(model.currentSong()->id, 103);
}

TEST_CASE("the position resolves the song when no id is known")
{
    PlaylistModel model;
    model.setSongs(threeSongs());
    model.setCurrentSong(-1, 2);

    REQUIRE(model.currentSong() != nullptr);
    CHECK_EQ(model.currentSong()->title, QStringLiteral("third"));
}

TEST_CASE("an unknown position yields no current song")
{
    PlaylistModel model;
    model.setSongs(threeSongs());
    model.setCurrentSong(999, 42);
    CHECK(model.currentSong() == nullptr);
}

TEST_CASE("currentSongChanged fires once per resolved song")
{
    PlaylistModel model;
    SongSignals emitted(model);

    model.setSongs(threeSongs());
    model.setCurrentSong(102, 1);
    model.setCurrentSong(102, 1);      // identical status tick
    model.setSongs(threeSongs());      // reload of the same queue

    CHECK_EQ(emitted.ids, QList<int>{102});
}

TEST_CASE("loading the queue after status announces the current song")
{
    PlaylistModel model;
    SongSignals emitted(model);

    // Status arrives before the queue has been read: nothing to resolve yet.
    model.setCurrentSong(102, 1);
    CHECK_EQ(emitted.ids, QList<int>{-1});

    model.setSongs(threeSongs());
    REQUIRE(emitted.ids.size() == 2);
    CHECK_EQ(emitted.ids.last(), 102);
}

TEST_CASE("clearing the queue drops the current song")
{
    PlaylistModel model;
    model.setSongs(threeSongs());
    model.setCurrentSong(101, 0);
    REQUIRE(model.currentSong() != nullptr);

    SongSignals emitted(model);
    model.clear();

    CHECK_EQ(model.rowCount({}), 0);
    CHECK(model.currentSong() == nullptr);
    CHECK_EQ(emitted.ids, QList<int>{-1});
}
