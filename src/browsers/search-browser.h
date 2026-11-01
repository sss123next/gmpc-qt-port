#pragma once

/**
 * @file search-browser.h
 * @brief MPD search browser — QLineEdit + QTreeView with search results.
 *
 * Сам запрос живёт в SearchEngine (worker-поток со своим mpd_connection): поиск
 * без совпадений стоит серверу ~2 с полного скана базы, поэтому он не имеет права
 * выполняться в GUI-потоке. Отсюда дебаунс ввода и окно в 200 трэков вместо
 * «всей базы».
 */

#include "browser-widget.h"
#include "core/search-engine.h"

#include <QAbstractTableModel>
#include <QVector>

class Connection;
class QComboBox;
class QLabel;
class QLineEdit;
class QTimer;
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
    ~SearchBrowser() override;

    // BrowserWidget interface
    void onActivate() override {}
    /// Уходя с экрана, бросаем незавершённый поиск: его ответ уже не нужен.
    void onDeactivate() override;
    void onConnectionChanged(bool connected) override;
    QString title() const override { return tr("Search"); }
    QString iconName() const override { return QStringLiteral("edit-find"); }

    /// The embedded tree view (for context menu setup).
    QTreeView *treeView() const { return view_; }

    /// Движок поиска — свой worker-поток и соединение; нужен тестам и настройке.
    SearchEngine *engine() const { return engine_; }

private slots:
    /** @brief Немедленный поиск (Enter, смена поля).
     *  Пустой ввод очищает результат: запрос «на пустое место» стоит серверу
     *  полный обход базы (~2 с), слать его нельзя.
     */
    void executeSearch();
    /// Called when the search field combo changes.
    void onFieldChanged(int index);

private:
    /// Перезапускает дебаунс-таймер — поиск уходит в движок только после паузы ввода.
    void scheduleSearch();
    SearchQuery currentQuery() const;
    void applyResults(const QVector<SearchHit> &hits, int total);
    void showStatusText(const QString &text);

    Connection *conn_;
    SearchEngine *engine_;
    QTimer *debounceTimer_;
    SearchResultModel *model_;
    QTreeView *view_;
    QLineEdit *searchEdit_;
    QComboBox *fieldCombo_;
    QLabel *countLabel_;

    /// Дебаунс ввода: редкий запрос стоит ~2 с, печатать по буквам с таким ценником нельзя.
    static constexpr int kDebounceMs = 300;
};
