/**
 * @file search-browser.cpp
 * @brief MPD search browser — performs full-text search on the music database.
 */

#include "search-browser.h"
#include "core/connection.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

// ── Search field combo ──────────────────────────────────────────────────────

/// Индексы элементов комбополя поиска (порядок addItem() в конструкторе).
enum SearchField {
    FieldAll = 0,
    FieldArtist,
    FieldAlbum,
    FieldTitle,
    FieldGenre,
    FieldComposer,
    FieldCount
};

/// Поле запроса по индексу комбполя: «All fields» = any-tag, а не семь AND-тегов.
static SearchQuery::Field searchFieldForIndex(int index)
{
    switch (index) {
    case FieldArtist:   return SearchQuery::Field::Artist;
    case FieldAlbum:    return SearchQuery::Field::Album;
    case FieldTitle:    return SearchQuery::Field::Title;
    case FieldGenre:    return SearchQuery::Field::Genre;
    case FieldComposer: return SearchQuery::Field::Composer;
    default:            return SearchQuery::Field::Any;
    }
}

// ── SearchResultModel ───────────────────────────────────────────────────────

SearchResultModel::SearchResultModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int SearchResultModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : items_.size();
}

int SearchResultModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant SearchResultModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= items_.size())
        return {};

    const auto &item = items_.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColArtist:   return item.artist;
        case ColTitle:    return item.title;
        case ColAlbum:    return item.album;
        case ColGenre:    return item.genre;
        case ColDuration: {
            int mins = item.duration / 60;
            int secs = item.duration % 60;
            return QStringLiteral("%1:%2")
                .arg(mins)
                .arg(secs, 2, 10, QLatin1Char('0'));
        }
        }
    }

    return {};
}

QVariant SearchResultModel::headerData(int section, Qt::Orientation orient,
                                       int role) const
{
    if (orient != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case ColArtist:   return tr("Artist");
    case ColTitle:    return tr("Title");
    case ColAlbum:    return tr("Album");
    case ColGenre:    return tr("Genre");
    case ColDuration: return tr("Length");
    }
    return {};
}

void SearchResultModel::setResults(const QVector<SearchResultItem> &items)
{
    beginResetModel();
    items_ = items;
    endResetModel();
}

const SearchResultItem *SearchResultModel::resultAt(int row) const
{
    if (row < 0 || row >= items_.size())
        return nullptr;
    return &items_.at(row);
}

// ── SearchBrowser ───────────────────────────────────────────────────────────

SearchBrowser::SearchBrowser(Connection *conn, QWidget *parent)
    : BrowserWidget(parent)
    , conn_(conn)
    , engine_(new SearchEngine(this))
    , debounceTimer_(new QTimer(this))
    , model_(new SearchResultModel(this))
    , view_(new QTreeView(this))
    , searchEdit_(new QLineEdit(this))
    , fieldCombo_(new QComboBox(this))
    , countLabel_(new QLabel(this))
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    // Top row: field combo + search input
    auto *topRow = new QHBoxLayout;
    fieldCombo_->addItem(tr("All fields"));
    fieldCombo_->addItem(tr("Artist"));
    fieldCombo_->addItem(tr("Album"));
    fieldCombo_->addItem(tr("Title"));
    fieldCombo_->addItem(tr("Genre"));
    fieldCombo_->addItem(tr("Composer"));
    fieldCombo_->setCurrentIndex(FieldAll);

    searchEdit_->setPlaceholderText(tr("Search songs\u2026"));

    // Дебаунс: печатать по буквам с запросом за ~2 с нельзя, поэтому ищем только
    // после паузы. Enter и смена поля ищут сразу (таймер при этом гасится).
    debounceTimer_->setSingleShot(true);
    debounceTimer_->setInterval(kDebounceMs);
    connect(debounceTimer_, &QTimer::timeout, this, &SearchBrowser::executeSearch);
    connect(searchEdit_, &QLineEdit::textChanged, this, &SearchBrowser::scheduleSearch);
    connect(searchEdit_, &QLineEdit::returnPressed,
            this, &SearchBrowser::executeSearch);
    connect(fieldCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SearchBrowser::onFieldChanged);

    // Ответы движка приходят из worker-потока через queued connection; устаревшие
    // (отменённые) SearchEngine отфильтровывает сам.
    connect(engine_, &SearchEngine::resultsReady, this,
            [this](quint64 /*requestId*/, const QVector<SearchHit> &hits, int total,
                   const QString &error) {
        if (!error.isEmpty()) {
            model_->setResults({});
            showStatusText(tr("Search failed: %1").arg(error));
            return;
        }
        applyResults(hits, total);
    });

    topRow->addWidget(fieldCombo_);
    topRow->addWidget(searchEdit_, 1);

    view_->setModel(model_);
    view_->setRootIsDecorated(false);
    view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view_->setSortingEnabled(true);
    view_->header()->setStretchLastSection(true);
    view_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

    countLabel_->setText(tr("Type to search"));

    lay->addLayout(topRow);
    lay->addWidget(view_);
    lay->addWidget(countLabel_);
}

SearchBrowser::~SearchBrowser()
{
    // Worker-поток может быть ещё в чтении ответа — отменяем до того, как виджет
    // перестанет принимать его сигналы.
    engine_->cancel();
}

void SearchBrowser::scheduleSearch()
{
    if (!currentQuery().isValid()) {
        // Пустой ввод убирает старые строки сразу: ждать дебаунс ради запроса,
        // который сервер всё равно не получит, смысла нет.
        debounceTimer_->stop();
        executeSearch();
        return;
    }
    debounceTimer_->start(kDebounceMs);
}

SearchQuery SearchBrowser::currentQuery() const
{
    SearchQuery q;
    q.term = searchEdit_->text().trimmed();
    q.field = searchFieldForIndex(fieldCombo_->currentIndex());
    return q;
}

void SearchBrowser::showStatusText(const QString &text)
{
    countLabel_->setText(text);
}

void SearchBrowser::onConnectionChanged(bool connected)
{
    setEnabled(connected);
    if (!conn_)
        return;

    if (!connected) {
        debounceTimer_->stop();
        engine_->cancel();
        model_->setResults({});
        showStatusText(tr("Disconnected"));
        return;
    }

    // Поиск ходит своим соединением (libmpdclient: один handle обслуживает один
    // поток), поэтому берём настройки того, с которым уже подключились.
    SearchEngine::Endpoint endpoint;
    endpoint.host = conn_->host();
    endpoint.port = conn_->port();
    endpoint.password = conn_->password();
    endpoint.timeoutMs = conn_->timeoutMs();
    engine_->setEndpoint(endpoint);
}

void SearchBrowser::onDeactivate()
{
    // Ответ ушедшего экрана не нужен никому, а стоит он до двух секунд скана базы.
    debounceTimer_->stop();
    engine_->cancel();
}

void SearchBrowser::executeSearch()
{
    debounceTimer_->stop();

    const SearchQuery query = currentQuery();
    if (!query.isValid()) {
        // Пустой запрос сервер оплачивает полным обходом базы, а результатов в нём
        // ровно ноль. Зачем платить — просто очищаем экран.
        model_->setResults({});
        showStatusText(tr("Type to search"));
        return;
    }

    engine_->search(query);   // предыдущий запрос движок отменяет сам
}

void SearchBrowser::applyResults(const QVector<SearchHit> &hits, int total)
{
    QVector<SearchResultItem> rows;
    rows.reserve(hits.size());
    for (const SearchHit &hit : hits) {
        SearchResultItem item;
        item.uri      = hit.uri;
        item.artist   = hit.artist;
        item.title    = hit.title;
        item.album    = hit.album;
        item.genre    = hit.genre;
        item.duration = hit.duration;
        rows.append(item);
    }
    model_->setResults(rows);

    if (total > static_cast<int>(rows.size())) {
        // Окно заполнено целиком — за ним ещё есть совпадения, сколько именно
        // сказал searchcount.
        showStatusText(tr("%1 of %2 matches")
                           .arg(static_cast<qlonglong>(rows.size()))
                           .arg(total));
    } else {
        showStatusText(tr("%n match(es)", "", static_cast<int>(rows.size())));
    }
}

void SearchBrowser::onFieldChanged(int index)
{
    // Смена поля меняет смысл уже набранного текста, поэтому ищем сразу, не дожидаясь
    // конца дебаунса.
    Q_UNUSED(index);
    searchEdit_->setFocus();
    if (currentQuery().isValid())
        executeSearch();
}
