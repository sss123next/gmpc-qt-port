/**
 * @file browser-stack.cpp
 * @brief Browser enable/activate logic.
 */

#include "browser-stack.h"

#include "browsers/browser-widget.h"

BrowserStack::BrowserStack(QStackedWidget *stack, QObject *parent)
    : QObject(parent), stack_(stack)
{
    connect(stack_, &QStackedWidget::currentChanged, this,
            [this](int index) { switchActiveTo(browserAt(index)); });
}

void BrowserStack::addBrowser(BrowserWidget *browser)
{
    if (!browser || browsers_.contains(browser))
        return;
    stack_->addWidget(browser);
    browsers_.append(browser);

    // The first page is the visible one even before the window is shown:
    // QStackedWidget only emits currentChanged() from its show event, which
    // would leave us without an active browser until the app is mapped.
    if (!active_) {
        active_ = browser;
        stack_->setCurrentIndex(0);
    }
}

BrowserWidget *BrowserStack::browserAt(int index) const
{
    if (index < 0 || index >= browsers_.size())
        return nullptr;
    return browsers_[index];
}

void BrowserStack::setActiveIndex(int index)
{
    stack_->setCurrentIndex(index);
}

void BrowserStack::switchActiveTo(BrowserWidget *next)
{
    if (next == active_ && (activated_ || !connected_))
        return;

    if (active_ && activated_) {
        active_->onDeactivate();
        activated_ = false;
    }

    active_ = next;
    if (active_ && connected_) {
        active_->onActivate();
        activated_ = true;
    }
}

void BrowserStack::setConnected(bool connected)
{
    if (!connected && active_ && activated_) {
        active_->onDeactivate();
        activated_ = false;
    }

    for (BrowserWidget *browser : browsers_)
        browser->onConnectionChanged(connected);

    connected_ = connected;

    if (connected && active_ && !activated_) {
        active_->onActivate();
        activated_ = true;
    }
}
