#pragma once

/**
 * @file search-browser.h
 * @brief MPD search browser — QLineEdit + QTreeView with search results.
 */

#include "browser-widget.h"

#include <QAbstractTableModel>
#include <QVector>

class Connection;
class QLineEdit;
class QTreeView;

// ── Search result model ─────────────────────────────────────────────────────

/// One search-result row.
struct SearchResultItem {
    QString uri;
    QString artist;
    QString title;
    QString album;
    QString genre;
    int duration = 0;
};

/// Table model for MPD search results.
class SearchResultModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColArtist = 0, ColTitle, ColAlbum, ColGenre, ColDuration,
                  ColCount };

    explicit SearchResultModel(QObject *parent = nullptr);
    ~SearchResultModel() override = default;

    // QAbstractItemModel
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orient,
                        int role = Qt::DisplayRole) const override;

    /// Replace all rows with new results.
    void setResults(const QVector<SearchResultItem> &items);

    /// Access result at given row.
    const SearchResultItem *resultAt(int row) const;

private:
    QVector<SearchResultItem> items_;
};

// ── SearchBrowser widget ────────────────────────────────────────────────────

/// Search the MPD database.
class SearchBrowser : public BrowserWidget {
    Q_OBJECT
public:
    explicit SearchBrowser(Connection *conn, QWidget *parent = nullptr);
    ~SearchBrowser() override = default;

    // BrowserWidget interface
    void onActivate() override {}
    void onDeactivate() override {}
    void onConnectionChanged(bool connected) override;
    QString title() const override { return tr("Search"); }
    QString iconName() const override { return QStringLiteral("edit-find"); }

private slots:
    void executeSearch();

private:
    Connection *conn_;
    SearchResultModel *model_;
    QTreeView *view_;
    QLineEdit *searchEdit_;
};
