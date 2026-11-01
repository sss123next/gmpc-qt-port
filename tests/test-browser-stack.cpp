#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-browser-stack.cpp
 * @brief Wiring tests for the browser lifecycle (enable on connect, activate
 *        on show).  These are behaviour tests: they count the callbacks every
 *        browser actually receives instead of checking that a widget exists.
 */

#include <QApplication>
#include <QStackedWidget>
#include <QString>

#include "gui/browser-stack.h"
#include "browsers/browser-widget.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QApplication app(argc, &argv);

/// Browser that records every lifecycle callback it is given.
class RecordingBrowser : public BrowserWidget {
public:
    explicit RecordingBrowser(const QString &name) : name_(name) {}

    void onActivate() override { ++activations; log += "a"; }
    void onDeactivate() override { ++deactivations; log += "d"; }
    void onConnectionChanged(bool connected) override {
        BrowserWidget::onConnectionChanged(connected);   // keeps enabled in sync
        ++connectionChanges;
        lastConnected = connected;
        log += connected ? "+" : "-";
    }

    QString title() const override { return name_; }

    int activations = 0;
    int deactivations = 0;
    int connectionChanges = 0;
    bool lastConnected = false;
    QString log;

private:
    QString name_;
};

namespace {

struct Fixture {
    QStackedWidget stack;
    BrowserStack browsers{&stack};
    RecordingBrowser *current = nullptr;
    RecordingBrowser *database = nullptr;
    RecordingBrowser *files = nullptr;

    explicit Fixture(bool connect = false)
        : current(new RecordingBrowser("Current"))
        , database(new RecordingBrowser("Database"))
        , files(new RecordingBrowser("Files"))
    {
        browsers.addBrowser(current);
        browsers.addBrowser(database);
        browsers.addBrowser(files);
        if (connect)
            browsers.setConnected(true);
    }
};

} // namespace

TEST_CASE("addBrowser appends pages in order and tracks them")
{
    Fixture f;
    CHECK_EQ(f.browsers.count(), 3);
    CHECK(f.browsers.browserAt(0) == f.current);
    CHECK(f.browsers.browserAt(1) == f.database);
    CHECK(f.browsers.browserAt(2) == f.files);
    CHECK_EQ(f.stack.count(), 3);
    CHECK_EQ(f.stack.currentIndex(), 0);
    CHECK(f.browsers.activeBrowser() == f.current);
}

TEST_CASE("browserAt is bounds-checked")
{
    Fixture f;
    CHECK(f.browsers.browserAt(-1) == nullptr);
    CHECK(f.browsers.browserAt(3) == nullptr);
}

TEST_CASE("addBrowser ignores duplicates and null")
{
    QStackedWidget stack;
    BrowserStack browsers(&stack);
    // addWidget takes ownership of the page, so it must be heap allocated.
    auto *one = new RecordingBrowser("one");

    browsers.addBrowser(one);
    browsers.addBrowser(one);
    browsers.addBrowser(nullptr);

    CHECK_EQ(browsers.count(), 1);
    CHECK_EQ(stack.count(), 1);
}

TEST_CASE("while disconnected nothing is activated and everything is disabled")
{
    Fixture f;
    f.browsers.setConnected(false);

    CHECK(f.browsers.isConnected() == false);
    CHECK_FALSE(f.current->isEnabled());
    CHECK_FALSE(f.database->isEnabled());
    CHECK_EQ(f.current->activations, 0);
    CHECK_EQ(f.database->activations, 0);

    // Switching pages must not start work in a browser that has no server.
    f.browsers.setActiveIndex(1);
    CHECK_EQ(f.database->activations, 0);
    CHECK_FALSE(f.database->isEnabled());
}

TEST_CASE("connecting enables every browser and activates only the visible one")
{
    Fixture f;
    f.browsers.setConnected(true);

    CHECK(f.browsers.isConnected());
    CHECK(f.current->isEnabled());
    CHECK(f.database->isEnabled());
    CHECK(f.files->isEnabled());

    // Every browser learns about the connection, but only the shown page loads.
    CHECK_EQ(f.current->connectionChanges, 1);
    CHECK_EQ(f.files->connectionChanges, 1);
    CHECK_EQ(f.current->activations, 1);
    CHECK_EQ(f.database->activations, 0);
    CHECK_EQ(f.files->deactivations, 0);
}

TEST_CASE("switching pages deactivates the old one and activates the new once")
{
    Fixture f(/*connect=*/true);
    REQUIRE_EQ(f.current->activations, 1);

    f.browsers.setActiveIndex(1);
    CHECK(f.browsers.activeBrowser() == f.database);
    CHECK_EQ(f.current->deactivations, 1);
    CHECK_EQ(f.database->activations, 1);
    // Callback order: connect notification, activation, then deactivation.
    CHECK(f.current->log.endsWith("ad"));
    CHECK(f.database->log == "+a");

    // Coming back re-activates: lazy browsers reload when shown again.
    f.browsers.setActiveIndex(0);
    CHECK_EQ(f.database->deactivations, 1);
    CHECK_EQ(f.current->activations, 2);
}

TEST_CASE("selecting the visible page again does not reactivate it")
{
    Fixture f(/*connect=*/true);
    f.browsers.setActiveIndex(0);   // already current
    CHECK_EQ(f.current->activations, 1);
    CHECK_EQ(f.current->deactivations, 0);
}

TEST_CASE("disconnecting deactivates the active browser and disables all")
{
    Fixture f(/*connect=*/true);
    f.browsers.setActiveIndex(2);
    REQUIRE_EQ(f.files->activations, 1);

    f.browsers.setConnected(false);

    CHECK_FALSE(f.current->isEnabled());
    CHECK_FALSE(f.files->isEnabled());
    CHECK_EQ(f.files->deactivations, 1);
    CHECK_EQ(f.current->connectionChanges, 2);   // initial false + this one
}

TEST_CASE("reconnecting activates the page that is still visible")
{
    Fixture f(/*connect=*/true);
    f.browsers.setActiveIndex(1);
    const int before = f.database->activations;

    f.browsers.setConnected(false);
    f.browsers.setConnected(true);

    CHECK(f.database->isEnabled());
    CHECK_EQ(f.database->activations, before + 1);
    CHECK_EQ(f.current->activations, 1);   // hidden page stays untouched
}
