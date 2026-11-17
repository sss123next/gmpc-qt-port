#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-metadata-leak.cpp
 * @brief Stress-test the metadata pipeline: cycle track metadata many times
 *        to detect memory leaks under AddressSanitizer.
 *
 * Creates a MetadataManager, cycles through different artist/album pairs
 * (each triggers a full cascade: cache → local → embedded → MusicBrainz
 * → Last.FM → Deezer → Discogs → notFound).  Since there's no network
 * and no real files, every path is exercised up to the first network
 * attempt, which fails fast.  The test passes if it doesn't crash; ASan
 * reports any leaks at exit.
 */

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include "metadata/metadata-manager.h"
#include "metadata/cache-sqlite.h"
#include "metadata/cover-provider.h"
#include "tools/async-download.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QCoreApplication app(argc, &argv);

TEST_CASE("100 track changes with metadata cascade")
{
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    const QString cachePath = tmpDir.filePath("cache.db");
    MetadataManager mgr(cachePath,          // cache
                        QString(),          // Last.FM key (empty → skip)
                        QString(),          // Discogs token (empty → skip)
                        QString(),          // musicDir (empty → skip local)
                        nullptr);           // parent

    // 100 different artists/albums — no duplicates, so cache never hits,
    // forcing the full cascade every time.
    for (int i = 0; i < 100; ++i) {
        const QString artist = QStringLiteral("Artist%1").arg(i);
        const QString album  = QStringLiteral("Album%1").arg(i % 20);
        const QString uri    = QStringLiteral("file%1.flac").arg(i);

        mgr.requestCover(artist, album, uri);
        mgr.requestLyrics(artist, QStringLiteral("Song%1").arg(i), uri);

        // Process events so AsyncDownload gets to cancel/fail replies.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    // One final event-loop drain so pending deleteLater() events fire.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
}
