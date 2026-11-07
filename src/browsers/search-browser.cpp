/**
 * @file search-browser.cpp
 * @brief MPD search browser — performs full-text search on the music database.
 */

#include "search-browser.h"
#include "core/connection.h"
#include "core/mpd-commands.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
#include <QShortcut>
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

    // Числом, а не строкой: по «10:00» / «9:59» порядок был бы алфавитным.
    if (role == DurationRole)
        return item.duration;

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

void SearchResultModel::appendResults(const QVector<SearchResultItem> &items)
{
    if (items.isEmpty())
        return;

    // Вставкой, а не reset: автодогрузка срабатывает у нижней границы списка, и полный
    // сброс модели вернул бы прокрутку в начало — ровно туда, откуда её тянут.
    const int first = items_.size();
    beginInsertRows({}, first, first + items.size() - 1);
    items_ += items;
    endInsertRows();
}

bool SearchResultProxy::lessThan(const QModelIndex &left,
                                 const QModelIndex &right) const
{
    if (left.column() == right.column()
        && left.column() == SearchResultModel::ColDuration)
        return left.data(SearchResultModel::DurationRole).toInt()
             < right.data(SearchResultModel::DurationRole).toInt();

    // Без регистра: «abba» и «ABBA» — один артист, а не два разных порядка.
    const QString l = left.data(Qt::DisplayRole).toString();
    const QString r = right.data(Qt::DisplayRole).toString();
    return QString::compare(l, r, Qt::CaseInsensitive) < 0;
}

const SearchResultItem *SearchResultModel::resultAt(int row) const
{
    if (row < 0 || row >= items_.size())
        return nullptr;
    return &items_.at(row);
}

// ── SearchBrowser ───────────────────────────────────────────────────────────

SearchBrowser::SearchBrowser(Connection *conn, MpdCommands *cmds, QWidget *parent)
    : BrowserWidget(parent)
    , conn_(conn)
    , cmds_(cmds)
    , engine_(new SearchEngine(this))
    , debounceTimer_(new QTimer(this))
    , model_(new SearchResultModel(this))
    , proxy_(new SearchResultProxy(this))
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
            // Отказ сбрасывает и состояние пагинации: догружать поверх ошибки нечего.
            appending_ = false;
            totalMatches_ = -1;
            windowWasFull_ = false;
            model_->setResults({});
            showStatusText(tr("Search failed: %1").arg(error));
            return;
        }
        applyResults(hits, total);
    });

    topRow->addWidget(fieldCombo_);
    topRow->addWidget(searchEdit_, 1);

    proxy_->setSourceModel(model_);
    view_->setModel(proxy_);
    view_->setRootIsDecorated(false);
    view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // Строкой, а не ячейкой: selectedRows(0) (на нём стоит uniqueSelectedRows) отдаёт
    // строки только при выделении всей строки — иначе Ctrl-клик по нескольким трэкам
    // оставил бы в меню лишь текущий.
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Сортировка идёт через прокси: у модели своего sort() нет, и без него клик по
    // заголовку молча ничего не делал (остаток 6.5).
    view_->setSortingEnabled(true);
    view_->sortByColumn(-1, Qt::AscendingOrder);   // −1 — «как отдал сервер»
    view_->header()->setStretchLastSection(true);
    view_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

    // ── действия по клавишам на результатах (как Insert/Ctrl+Insert в оригинале) ──
    for (const QKeySequence &seq : {QKeySequence(Qt::Key_Return),
                                    QKeySequence(Qt::Key_Enter),
                                    QKeySequence(Qt::Key_Insert)}) {
        auto *sc = new QShortcut(seq, view_);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated,
                this, &SearchBrowser::addSelectionToQueue);
    }
    // Ctrl+Insert заменяет очередь: clear → add выделенного → play (оригинал делал то же).
    auto *replaceSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Insert), view_);
    replaceSc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(replaceSc, &QShortcut::activated,
            this, &SearchBrowser::replaceQueueWithSelection);

    // История сессии: Ctrl+↑/↓ листают термины прямо в поле поиска.
    auto *prevSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Up), searchEdit_);
    prevSc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(prevSc, &QShortcut::activated,
            this, &SearchBrowser::recallPreviousQuery);

    auto *nextSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Down), searchEdit_);
    nextSc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(nextSc, &QShortcut::activated,
            this, &SearchBrowser::recallNextQuery);

    // Автодогрузка: доехали ползунком до конца — просим следующее окно. Полный сброс
    // модели тут не годится (прокрутка у нижней границы), поэтому строки дописываются.
    connect(view_->verticalScrollBar(), &QAbstractSlider::valueChanged,
            this, &SearchBrowser::maybeLoadMore);

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

QVector<BrowserIntent> SearchBrowser::selectedIntents() const
{
    QVector<BrowserIntent> intents;
    // Индексы приходят из прокси: строку в модели ищет mapToSource, а не номер вида —
    // после клика по заголовку они расходятся.
    for (const QModelIndex &idx : uniqueSelectedRows(view_)) {
        const SearchResultItem *hit = model_->resultAt(proxy_->mapToSource(idx).row());
        if (!hit || hit->uri.isEmpty())
            continue;
        intents.append(BrowserIntent{BrowserIntent::Kind::Song, hit->uri, {}});
    }
    return intents;
}

void SearchBrowser::addSelectionToQueue()
{
    const QVector<BrowserIntent> intents = selectedIntents();
    if (!cmds_ || intents.isEmpty())
        return;

    // По одной команде на строку: `load` тут не годится — это результат поиска, а не
    // сохранённый список. Порядок сервер сохранит тот же (команды идут по соединению).
    for (const BrowserIntent &intent : intents)
        cmds_->addUri(intent.uri);

    showStatusText(tr("Added %n song(s)", "", static_cast<int>(intents.size())));
}

void SearchBrowser::replaceQueueWithSelection()
{
    const QVector<BrowserIntent> intents = selectedIntents();
    if (!cmds_ || intents.isEmpty())
        return;

    // Как pl3_find2_browser_replace_selected(): очистить очередь, положить выделенное,
    // играть. Очередь пользователя при этом выбрасывается — потому и отдельная клавиша.
    cmds_->clearQueue();
    for (const BrowserIntent &intent : intents)
        cmds_->addUri(intent.uri);
    cmds_->play();

    showStatusText(tr("Replaced queue with %n song(s)", "",
                      static_cast<int>(intents.size())));
}

void SearchBrowser::rememberTerm(const QString &term)
{
    if (history_.isEmpty() || history_.last() != term) {
        history_ << term;
        while (history_.size() > kHistoryMax)
            history_.removeFirst();
    }
    // Поиск термина сбрасывает листание: следующий Ctrl+↑ идёт от свежего конца.
    historyPos_ = -1;
}

void SearchBrowser::applyHistoryTerm(const QString &term)
{
    searchEdit_->setText(term);
    // Дебаунс не нужен — термин уже был найден, а ждать 300 мс после Ctrl+↑ значит
    // показывать старый результат ещё треть секунды.
    debounceTimer_->stop();
}

void SearchBrowser::recallPreviousQuery()
{
    if (history_.isEmpty())
        return;

    const int target = historyPos_ < 0 ? history_.size() - 2 : historyPos_ - 1;
    // Один термин в истории: «предыдущий» — он же; ниже нуля не уходим.
    historyPos_ = qMax(0, target);
    applyHistoryTerm(history_.at(historyPos_));
}

void SearchBrowser::recallNextQuery()
{
    if (historyPos_ < 0 || historyPos_ + 1 >= history_.size())
        return;

    ++historyPos_;
    applyHistoryTerm(history_.at(historyPos_));
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
        // Состояние пагинации тоже сбрасывается: после переподключения окно читается
        // заново, а «догрузить поверх» относилось бы к прошлому серверу.
        appending_ = false;
        totalMatches_ = -1;
        windowWasFull_ = false;
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
        appending_ = false;
        totalMatches_ = -1;
        windowWasFull_ = false;
        model_->setResults({});
        showStatusText(tr("Type to search"));
        return;
    }

    rememberTerm(query.term);
    // Новый термин: окно с начала, ответ заменяет строки (а не дописывает их).
    appending_ = false;
    startRequest(query);
}

void SearchBrowser::startRequest(const SearchQuery &query)
{
    requested_ = query;
    engine_->search(query);   // предыдущий запрос движок отменяет сам
}

bool SearchBrowser::mayHaveMore() const
{
    if (totalMatches_ >= 0)
        return model_->rowCount({}) < totalMatches_;
    // Счётчик сервер не сообщил: признак «есть ещё» — полное прошлое окно.
    return windowWasFull_;
}

void SearchBrowser::maybeLoadMore(int scrollValue)
{
    if (appending_ || !requested_.isValid())
        return;

    auto *bar = view_->verticalScrollBar();
    // Ползунок доехал до конца и за ним ещё есть совпадения — просим следующее окно.
    if (scrollValue < bar->maximum() || model_->rowCount({}) == 0)
        return;
    if (!mayHaveMore())
        return;

    SearchQuery next = requested_;
    next.offset = static_cast<unsigned>(model_->rowCount({}));
    appending_ = true;   // ответ дописываем, а не заменяем: прокрутка стоит у конца
    startRequest(next);
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

    // Полное окно — признак, что за ним может быть ещё: счётчик сервер отдаёт не всегда,
    // а по пустому «хвосту» автодогрузка должна понимать, что дальше ничего нет.
    const bool appendNow = appending_;
    windowWasFull_ = rows.size() >= static_cast<int>(requested_.clampedLimit());
    totalMatches_ = total;
    appending_ = false;

    if (appendNow)
        model_->appendResults(rows);   // следующее окно: прокрутка стоит у конца
    else
        model_->setResults(rows);

    const int shown = static_cast<int>(model_->rowCount({}));
    if (total > shown) {
        // Показана только часть совпадений, сколько именно — сказал searchcount;
        // остальное подтянется прокруткой.
        showStatusText(tr("%1 of %2 matches")
                           .arg(static_cast<qlonglong>(shown))
                           .arg(total));
    } else {
        showStatusText(tr("%n match(es)", "", shown));
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
