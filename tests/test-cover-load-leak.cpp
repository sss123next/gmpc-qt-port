/**
 * @file test-cover-load-leak.cpp
 * @brief Stress-test the cover-loading pipeline with real QPixmap rendering.
 *
 * Creates a CoverWidget wired to MetadataManager and cycles through
 * cover requests.  Unlike test-metadata-leak, this exercises the Qt
 * widget rendering path that actually allocates QPixmap backing store.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <QApplication>
#include <QTemporaryDir>
#include <QWidget>
#include <QVBoxLayout>
#include <QPixmap>

#include "metadata/metadata-manager.h"
#include "widgets/cover-widget.h"

static int s_argc = 1;
static char s_arg0[] = "gmpc-qt-test";
static char *s_argv = s_arg0;

/// A 1×1 red PNG (68 bytes) — enough for QPixmap to decode and allocate.
static const unsigned char kFakeCoverPng[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
    0xde, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41,
    0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0x00,
    0x00, 0x00, 0x03, 0x00, 0x01, 0x37, 0x82, 0x15,
    0xdb, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
    0x44, 0xae, 0x42, 0x60, 0x82
};

TEST_CASE("CoverWidget setCover/clear cycle — 100 iterations")
{
    QApplication app(s_argc, &s_argv);

    QWidget window;
    auto *lay = new QVBoxLayout(&window);
    CoverWidget cw(&window);
    cw.setCoverSize(256);
    lay->addWidget(&cw);

    const QByteArray fakeCover(reinterpret_cast<const char*>(kFakeCoverPng),
                               sizeof(kFakeCoverPng));

    for (int i = 0; i < 100; ++i) {
        QPixmap pm;
        pm.loadFromData(fakeCover);
        cw.setCover(pm);
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        cw.clear();
    }
}

TEST_CASE("Full pipeline: MetadataManager + CoverWidget via local file — 100 iterations")
{
    QApplication app(s_argc, &s_argv);

    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    // Create a music dir with a cover file in a subdirectory.
    const QString musicDir = tmpDir.filePath("music");
    QDir().mkpath(musicDir + "/artist/album");

    const QString coverPath = musicDir + "/artist/album/cover.jpg";
    {
        QFile f(coverPath);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(reinterpret_cast<const char*>(kFakeCoverPng), sizeof(kFakeCoverPng));
    }

    MetadataManager mgr(tmpDir.filePath("cache.db"),
                        QString(),   // Last.FM key — empty
                        QString(),   // Discogs token — empty
                        musicDir,    // musicDir for local file lookup
                        nullptr);

    QWidget window;
    auto *lay = new QVBoxLayout(&window);
    CoverWidget cw(&window);
    cw.setCoverSize(256);
    lay->addWidget(&cw);
    cw.setMetadataManager(&mgr);

    for (int i = 0; i < 100; ++i) {
        const QString artist = QStringLiteral("Artist");
        const QString album  = QStringLiteral("Album");
        const QString uri    = QStringLiteral("artist/album/song.flac");

        cw.fetchCover(artist, album, uri);
        QApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    QApplication::processEvents(QEventLoop::AllEvents, 200);
}

TEST_CASE("MetadataManager + CoverProvider cascade (all fail) — 100 iterations")
{
    QApplication app(s_argc, &s_argv);

    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    MetadataManager mgr(tmpDir.filePath("cache.db"),
                        QString(), QString(), QString(), nullptr);

    QWidget window;
    auto *lay = new QVBoxLayout(&window);
    CoverWidget cw(&window);
    cw.setCoverSize(256);
    lay->addWidget(&cw);
    cw.setMetadataManager(&mgr);

    for (int i = 0; i < 100; ++i) {
        const QString artist = QStringLiteral("Artist%1").arg(i);
        const QString album  = QStringLiteral("Album%1").arg(i % 20);
        const QString uri    = QStringLiteral("file%1.flac").arg(i);

        cw.fetchCover(artist, album, uri);
        QApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    QApplication::processEvents(QEventLoop::AllEvents, 200);
}

