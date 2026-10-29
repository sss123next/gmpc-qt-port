#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-progress-slider.cpp
 * @brief Tests for ProgressSlider.
 */

#include <QApplication>

#include "widgets/progress-slider.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QApplication app(argc, &argv);

TEST_CASE("ProgressSlider is initially at 0")
{
    ProgressSlider s(Qt::Horizontal);
    CHECK_EQ(s.value(), 0);
}

TEST_CASE("ProgressSlider range is 0-1000")
{
    ProgressSlider s(Qt::Horizontal);
    CHECK_EQ(s.minimum(), 0);
    CHECK_EQ(s.maximum(), 1000);
}

TEST_CASE("ProgressSlider setPosition updates value")
{
    ProgressSlider s(Qt::Horizontal);
    s.setPosition(30, 120); // 25%
    CHECK_EQ(s.value(), 250); // 250‰
}

TEST_CASE("ProgressSlider setPosition clamps correctly")
{
    ProgressSlider s(Qt::Horizontal);
    s.setPosition(0, 100);
    CHECK_EQ(s.value(), 0);
    s.setPosition(100, 100);
    CHECK_EQ(s.value(), 1000);
}

TEST_CASE("ProgressSlider setPosition with zero total is safe")
{
    ProgressSlider s(Qt::Horizontal);
    s.setPosition(50, 0); // no division by zero
    CHECK_EQ(s.value(), 0);
}

TEST_CASE("ProgressSlider setTotal does nothing (normalized range)")
{
    ProgressSlider s(Qt::Horizontal);
    s.setTotal(9999);
    // range unchanged
    CHECK_EQ(s.minimum(), 0);
    CHECK_EQ(s.maximum(), 1000);
}
