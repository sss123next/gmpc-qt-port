#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-progress-slider.cpp
 * @brief Tests for ProgressSlider.
 */

#include <QApplication>
#include <QMouseEvent>
#include <QSignalSpy>

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

// ── Перемотка: отпущенный ползунок ⇒ seek в секундах ─────────────────────────
// Виджет не показан, поэтому QTest::mousePress() не годится (ему нужно окно) —
// события уходят прямо в QWidget::event().

static void sendButtonEvent(ProgressSlider &s, QEvent::Type type, int x)
{
    const QPointF pos(x, 5);
    const Qt::MouseButton buttons = type == QEvent::MouseButtonRelease
        ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent ev(type, pos, s.mapToGlobal(pos.toPoint()), Qt::LeftButton, buttons,
                   Qt::NoModifier);
    QApplication::sendEvent(&s, &ev);
}

TEST_CASE("ProgressSlider emits seekRequested in seconds on release")
{
    ProgressSlider s(Qt::Horizontal);
    QSignalSpy spy(&s, &ProgressSlider::seekRequested);
    REQUIRE(spy.isValid());

    s.setPosition(0.0f, 120.0f);          // трек длиной 2:00
    sendButtonEvent(s, QEvent::MouseButtonPress, 5);
    s.setValue(250);                      // четверть промиллевой шкалы
    sendButtonEvent(s, QEvent::MouseButtonRelease, 5);

    REQUIRE_EQ(spy.count(), 1);
    CHECK_EQ(spy.at(0).at(0).toInt(), 30);   // секунды, а не «250 промилле»
}

TEST_CASE("ProgressSlider releases the end of the scale as the full duration")
{
    ProgressSlider s(Qt::Horizontal);
    QSignalSpy spy(&s, &ProgressSlider::seekRequested);

    s.setPosition(10.0f, 200.0f);
    sendButtonEvent(s, QEvent::MouseButtonPress, 5);
    s.setValue(1000);
    sendButtonEvent(s, QEvent::MouseButtonRelease, 95);

    REQUIRE_EQ(spy.count(), 1);
    CHECK_EQ(spy.at(0).at(0).toInt(), 200);
}

TEST_CASE("ProgressSlider ignores progress updates while dragging")
{
    ProgressSlider s(Qt::Horizontal);
    sendButtonEvent(s, QEvent::MouseButtonPress, 5);
    s.setValue(400);

    s.setPosition(118.0f, 120.0f);        // polling почти доел трек
    CHECK_EQ(s.value(), 400);             // перетаскивание не перебито

    sendButtonEvent(s, QEvent::MouseButtonRelease, 5);
    s.setPosition(60.0f, 120.0f);         // после релиза polling снова хозяин
    CHECK_EQ(s.value(), 500);
}

TEST_CASE("ProgressSlider emits no seek without a known track length")
{
    ProgressSlider s(Qt::Horizontal);
    QSignalSpy spy(&s, &ProgressSlider::seekRequested);

    sendButtonEvent(s, QEvent::MouseButtonPress, 5);   // total_ ещё не задан
    s.setValue(700);
    sendButtonEvent(s, QEvent::MouseButtonRelease, 5);

    CHECK_EQ(spy.count(), 0);
}
