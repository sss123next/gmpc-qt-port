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
#include "browser-intent.h"
#include "core/search-engine.h"

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QVector>

class Connection;
class MpdCommands;
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

    /** @brief Длительность числом — для сортировки по клику заголовка.
     *
     *  По `DisplayRole` («10:00» / «9:59») порядок был бы алфавитным, и десятиминутный
     *  трэк встал бы перед девятисекундным; та же причина, что у `PlaylistModel::SortRole`.
     */
    static constexpr int DurationRole = Qt::UserRole + 1;

    /// Replace all rows with new results.
    void setResults(const QVector<SearchResultItem> &items);

    /** @brief Дописать строки в конец — следующее окно пагинации.
     *  Отдельно от `setResults()`: автодогрузка не должна сбрасывать прокрутку и
     *  выделение уже показанного (пользователь как раз у нижней границы).
     */
    void appendResults(const QVector<SearchResultItem> &items);

    /// Access result at given row.
    const SearchResultItem *resultAt(int row) const;

private:
    QVector<SearchResultItem> items_;
};

/** @brief Сортировка результатов по клику заголовка.
 *
 *  Нужна потому, что `QTreeView::setSortingEnabled(true)` без прокси ничего не делает
 *  при модели без собственного `sort()`: Qt зовёт `model->sort()`, а у нас её нет —
 *  клик по «Length» молча сортировал бы алфавитно.
 */
class SearchResultProxy : public QSortFilterProxyModel {
public:
    explicit SearchResultProxy(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) {}

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
};

// ── SearchBrowser widget ────────────────────────────────────────────────────

/// Search the MPD database.
class SearchBrowser : public BrowserWidget {
    Q_OBJECT
public:
    /** @param conn  соединение (нужно ради настроек сервера для движка)
     *  @param cmds  команды очереди: Enter/Insert в результатах кладут трэки, а
     *               Ctrl+Insert заменяет ими очередь. Без них действия молчат —
     *               так сьюты строят виджет ради самого поиска.
     */
    explicit SearchBrowser(Connection *conn, MpdCommands *cmds = nullptr,
                           QWidget *parent = nullptr);
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

    /** @brief Намерения выделенных строк результата: только трэки со своим URI.
     *  Групп тегов здесь нет, поэтому и ограничений `findadd` тоже нет — меню для
     *  поиска отличается от таг-браузера ровно этим.
     */
    [[nodiscard]] QVector<BrowserIntent> selectedIntents() const;

    /** @brief Toggle live search (textChanged) vs Enter-only. */
    void setSearchAsType(bool on);

    /** @brief Сколько строк результата уже показано (для тестов автодогрузки). */
    [[nodiscard]] int shownCount() const { return model_->rowCount({}); }

    /** @brief Запрос, ушедший в движок последним: пагинация видна только так.
     *
     *  `SearchEngine::search()` невиртуален (свой worker), поэтому «какой offset
     *  запрошен» снаружи иначе не узнать — тест читает это поле.
     */
    [[nodiscard]] SearchQuery lastRequested() const { return requested_; }

private slots:
    /** @brief Немедленный поиск (Enter, смена поля).
     *  Пустой ввод очищает результат: запрос «на пустое место» стоит серверу
     *  полный обход базы (~2 с), слать его нельзя.
     */
    void executeSearch();
    /// Called when the search field combo changes.
    void onFieldChanged(int index);
    /** @brief Enter/Insert на результатах — положить выделенные трэки в очередь. */
    void addSelectionToQueue();
    /** @brief Ctrl+Insert: очистить очередь, положить выделенное и играть (как оригинал). */
    void replaceQueueWithSelection();
    /// Ctrl+↑ по полю поиска — предыдущий запрос из истории сессии.
    void recallPreviousQuery();
    /// Ctrl+↓ — следующий запрос истории.
    void recallNextQuery();

private:
    /// Перезапускает дебаунс-таймер — поиск уходит в движок только после паузы ввода.
    void scheduleSearch();
    SearchQuery currentQuery() const;
    /** @brief Отправляет запрос как есть (пагинация зовёт с ненулевым offset). */
    void startRequest(const SearchQuery &query);
    /// Кладёт термин в историю сессии, если он не совпал с последним.
    void rememberTerm(const QString &term);
    /** @brief Догрузка следующего окна при прокрутке до конца. */
    void maybeLoadMore(int scrollValue);
    [[nodiscard]] bool mayHaveMore() const;
    /** @brief Раскладывает ответ движка по строкам: заменой или дописыванием.
     *  Дописывание — только для автодогрузки (флаг `appending_`).
     */
    void applyResults(const QVector<SearchHit> &hits, int total);
    /// Текст под таблицей («N из M совпадений», «Added N song(s)» и пр.).
    void showStatusText(const QString &text);
    /// Заполняет поле текстом из истории (без нового дебаунса).
    void applyHistoryTerm(const QString &term);

    Connection *conn_;
    MpdCommands *cmds_;
    SearchEngine *engine_;
    QTimer *debounceTimer_;
    SearchResultModel *model_;
    /** @brief Прокси между моделью и видом: клик по заголовку сортирует.
     *
     *  `setSortingEnabled(true)` при модели без `sort()` не делает ничего, поэтому
     *  сортировка живёт здесь — заодно она числовая для колонки «Length».
     */
    SearchResultProxy *proxy_;
    QTreeView *view_;
    QLineEdit *searchEdit_;
    QComboBox *fieldCombo_;
    QLabel *countLabel_;

    /// История терминов сессии (старые в начале); переживает только запуск виджета.
    QStringList history_;
    int historyPos_ = -1;   ///< −1 — «не листаем историю»
    SearchQuery requested_;  ///< последний ушедший запрос
    bool appending_ = false;        ///< ответ на этот запрос надо дописать, а не заменить
    int totalMatches_ = -1;         ///< сколько всего нашлось (−1 — сервер не сказал)
    bool windowWasFull_ = false;    ///< прошлое окно пришло целиком ⇒ за ним может быть ещё

    /// Дебаунс ввода: редкий запрос стоит ~2 с, печатать по буквам с таким ценником нельзя.
    static constexpr int kDebounceMs = 300;
    /// Сколько терминов помним для Ctrl+↑ (переживает только сессию виджета).
    static constexpr int kHistoryMax = 20;

    /// Tracking connection for setSearchAsType toggle.
    QMetaObject::Connection textChangedConn_;
};
