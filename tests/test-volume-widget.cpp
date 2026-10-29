#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-volume-widget.cpp
 * @brief Tests for VolumeWidget.
 */

#include <QApplication>
#include <QLabel>
#include <QObject>
#include <QSlider>

#include "widgets/volume-widget.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QApplication app(argc, &argv);

TEST_CASE("VolumeWidget contains slider and label")
{
    VolumeWidget w;
    auto *slider = w.findChild<QSlider *>();
    REQUIRE(slider != nullptr);
    auto *label = w.findChild<QLabel *>();
    REQUIRE(label != nullptr);
}

TEST_CASE("VolumeWidget slider range is 0-100")
{
    VolumeWidget w;
    auto *slider = w.findChild<QSlider *>();
    REQUIRE(slider != nullptr);
    CHECK_EQ(slider->minimum(), 0);
    CHECK_EQ(slider->maximum(), 100);
}

TEST_CASE("VolumeWidget setVolume updates slider")
{
    VolumeWidget w;
    w.setVolume(42);
    auto *slider = w.findChild<QSlider *>();
    REQUIRE(slider != nullptr);
    CHECK_EQ(slider->value(), 42);
}

TEST_CASE("VolumeWidget setVolume updates label")
{
    VolumeWidget w;
    w.setVolume(75);
    auto *label = w.findChild<QLabel *>();
    REQUIRE(label != nullptr);
    CHECK_EQ(label->text(), "75%");
}

TEST_CASE("VolumeWidget emits volumeChanged on slider change")
{
    VolumeWidget w;
    int emittedValue = -1;
    QObject::connect(&w, &VolumeWidget::volumeChanged,
            [&](int v) { emittedValue = v; });

    auto *slider = w.findChild<QSlider *>();
    REQUIRE(slider != nullptr);
    slider->setValue(33);

    CHECK_EQ(emittedValue, 33);
}

TEST_CASE("VolumeWidget label updates on slider change")
{
    VolumeWidget w;
    auto *slider = w.findChild<QSlider *>();
    REQUIRE(slider != nullptr);
    slider->setValue(88);

    auto *label = w.findChild<QLabel *>();
    REQUIRE(label != nullptr);
    CHECK_EQ(label->text(), "88%");
}
