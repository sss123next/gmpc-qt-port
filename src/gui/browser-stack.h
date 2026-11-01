#pragma once

/**
 * @file browser-stack.h
 * @brief Lifecycle owner for the browser panels of MainWindow's stack.
 *
 * A browser may only be usable while MPD is connected, and it must learn when
 * it becomes visible (to load its data lazily) or hidden (to stop working).
 * Keeping that decision here rather than inside MainWindow makes it testable
 * without an MPD connection: the previous code disabled every browser in its
 * constructor and never re-enabled it.
 */

#include <QList>
#include <QObject>
#include <QStackedWidget>

class BrowserWidget;

/// Enables browsers on connect and drives onActivate()/onDeactivate().
class BrowserStack : public QObject {
    Q_OBJECT
public:
    /** @param stack  The widget showing one browser at a time (not owned). */
    explicit BrowserStack(QStackedWidget *stack, QObject *parent = nullptr);

    /// Append @a browser to the stack and start tracking it.
    void addBrowser(BrowserWidget *browser);

    int count() const { return browsers_.size(); }
    /// Tracked browser at @a index (same order as the stack), or nullptr.
    BrowserWidget *browserAt(int index) const;
    /// The visible browser, or nullptr while the stack is empty.
    BrowserWidget *activeBrowser() const { return active_; }
    bool isConnected() const { return connected_; }

public slots:
    /** @brief Enable every browser on connect, disable them on disconnect.
     *
     *  On connect the visible browser is activated; on disconnect it is
     *  deactivated first so that it can release whatever it started loading.
     */
    void setConnected(bool connected);

    /// Make @a index the visible browser, deactivating the previous one.
    void setActiveIndex(int index);

private:
    void switchActiveTo(BrowserWidget *next);

    QStackedWidget *stack_;
    QList<BrowserWidget *> browsers_;
    BrowserWidget *active_ = nullptr;
    bool activated_ = false;   ///< active_ has an open onActivate() call.
    bool connected_ = false;
};
