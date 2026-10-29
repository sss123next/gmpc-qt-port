#ifndef GMPCQT_BROWSER_WIDGET_H
#define GMPCQT_BROWSER_WIDGET_H

/**
 * @file browser-widget.h
 * @brief Abstract base class for all browser panels.
 *
 * Each browser (database, file, search, playlist-editor, etc.)
 * implements the virtual interface below.  Browsers are stacked
 * inside MainWindow's central QStackedWidget and switched via
 * the sidebar.
 */

#include <QWidget>
#include <QString>
#include "core/connection.h"

/// Abstract interface for a sidebar-selectable browser panel.
class BrowserWidget : public QWidget {
    Q_OBJECT
public:
    explicit BrowserWidget(QWidget *parent = nullptr)
        : QWidget(parent) {}

    /// Called when the browser becomes active (shown in the stack).
    virtual void onActivate() {}

    /// Called when the browser becomes inactive (hidden).
    virtual void onDeactivate() {}

    /// Called when MPD connection state changes.
    virtual void onConnectionChanged(bool connected) {
        setEnabled(connected);
    }

    /// Human-readable title for the sidebar entry.
    virtual QString title() const = 0;

    /// Icon name (for QIcon::fromTheme or a fallback).
    virtual QString iconName() const { return {}; }
};

#endif // GMPCQT_BROWSER_WIDGET_H
