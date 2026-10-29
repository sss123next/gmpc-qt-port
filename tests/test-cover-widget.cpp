#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-cover-widget.cpp
 * @brief Tests for CoverWidget.
 */

#include <QApplication>
#include <QPixmap>

#include "widgets/cover-widget.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QApplication app(argc, &argv);

TEST_CASE("CoverWidget initially shows placeholder text")
{
    CoverWidget w;
    CHECK_FALSE(w.text().isEmpty());
    CHECK(w.text().contains("cover", Qt::CaseInsensitive));
}

TEST_CASE("CoverWidget setCover displays pixmap")
{
    CoverWidget w;
    QPixmap pm(64, 64);
    pm.fill(Qt::red);
    w.setCover(pm);
    // After setCover the text should be empty, pixmap present
    QPixmap pm2 = w.pixmap();
    CHECK_FALSE(pm2.isNull());
    CHECK(w.text().isEmpty());
}

TEST_CASE("CoverWidget clear removes pixmap and restores text")
{
    CoverWidget w;
    QPixmap pm(32, 32);
    pm.fill(Qt::blue);
    w.setCover(pm);

    w.clear();
    QPixmap pm2 = w.pixmap();
    CHECK(pm2.isNull());
    CHECK_FALSE(w.text().isEmpty());
}

TEST_CASE("CoverWidget has minimum size")
{
    CoverWidget w;
    CHECK(w.minimumWidth() >= 100);
    CHECK(w.minimumHeight() >= 100);
}
