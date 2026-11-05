#pragma once

/**
 * @file browser-widget.h
 * @brief Abstract base class for all browser panels.
 *
 * Each browser (database, file, search, playlist-editor, etc.)
 * implements the virtual interface below.  Browsers are stacked
 * inside MainWindow's central QStackedWidget and switched via
 * the sidebar.
 */

#include <QAbstractItemView>
#include <QItemSelectionModel>
#include <QList>
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

protected:
    /** @brief Выделенные строки вида — по одной на строку; при пустом выделении текущая.
     *  selectedIndexes() отдаёт по индексу на колонку, поэтому меню получало бы одну и ту
     *  же строку трижды; selectedRows() работает по диапазонам и потому не зависит от того,
     *  выделяли ли строку целиком или отдельную ячейку.
     */
    static QList<QModelIndex> uniqueSelectedRows(const QAbstractItemView *view)
    {
        if (!view || !view->selectionModel())
            return {};

        QList<QModelIndex> rows = view->selectionModel()->selectedRows(0);
        if (rows.isEmpty() && view->currentIndex().isValid())
            rows << view->currentIndex();
        return rows;
    }
};

