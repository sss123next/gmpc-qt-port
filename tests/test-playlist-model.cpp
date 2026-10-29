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
