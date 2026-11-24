/**
 * @file database-browser.cpp
 * @brief Database: дерево групп/альбомов и таблица трэков выбранного узла.
 */

#include "database-browser.h"
#include "core/config.h"
#include "gui/column-chooser-dialog.h"
#include "widgets/cover-viewer.h"
#include "widgets/cover-widget.h"

#include <mpd/tag.h>

#include "tools/log.h"
#include <algorithm>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QSplitter>
#include <QTableView>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>

namespace {

/// Вариант группировки: тег и подпись.
struct TagChoice {
    enum mpd_tag_type tag;
    QString label;
};

constexpr int kFilterDebounceMs = 200;

} // namespace

// ── Фильтр дерева ─────────────────────────────────────────────────────────

DatabaseTreeProxy::DatabaseTreeProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setRecursiveFilteringEnabled(true);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
}

// Прокси обязан пробрасывать ленивую загрузку в источник: собственный
// `canFetchMore` по умолчанию отвечает «да», а `fetchMore` ничего не делает,
/// из-за чего раскрытие группы оставляло пустой список альбомов.
bool DatabaseTreeProxy::canFetchMore(const QModelIndex &parent) const
{
    if (!sourceModel() || !parent.isValid())
        return false;
   return sourceModel()->canFetchMore(mapToSource(parent));
}

void DatabaseTreeProxy::fetchMore(const QModelIndex &parent)
{
    if (sourceModel() && parent.isValid())
        sourceModel()->fetchMore(mapToSource(parent));
}

bool DatabaseTreeProxy::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    if (filterRegularExpression().pattern().isEmpty())
        return true;

    const QModelIndex idx = sourceModel()->index(row, 0, parent);
    // Имена узлов: альбомы находятся по подстроке так же, как группы; родитель
    // остаётся видимым за счёт recursiveFilteringEnabled.
    return idx.data(Qt::DisplayRole).toString().contains(filterRegularExpression());
}

// ── Construction ───────────────────────────────────────────────────────────

DatabaseBrowser::DatabaseBrowser(Connection *conn, MpdCommands *cmds,
                                 QWidget *parent)
    : BrowserWidget(parent)
    , conn_(conn)
    , cmds_(cmds)
    , model_(new MusicDatabaseModel(conn, cmds, this))
    , treeProxy_(new DatabaseTreeProxy(this))
    , tree_(new TreeView(this))
    , tagCombo_(new QComboBox(this))
    , filterEdit_(new QLineEdit(this))
    , filterTimer_(new QTimer(this))
    , trackModel_(new DatabaseTrackModel(this))
    , trackProxy_(new QSortFilterProxyModel(this))
    , trackTable_(new TableView(this))
    , trackFilter_(new QLineEdit(this))
{
    setupUi();
}

DatabaseBrowser::~DatabaseBrowser()
{
    delete coverViewer_;
}

void DatabaseBrowser::setupUi()
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    // Ширина скролл-бара на этой теме — для резерва от оверлей-скроллов.
    // ── Верхняя строка: способ группировки и фильтр дерева ───────────────
    auto *topBar = new QHBoxLayout;
    topBar->addWidget(new QLabel(tr("Browse by:"), this));
    tagCombo_->setObjectName(QStringLiteral("databaseGrouping"));
    connect(tagCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DatabaseBrowser::onTagChanged);
    topBar->addWidget(tagCombo_);

    filterEdit_->setObjectName(QStringLiteral("databaseFilter"));
    filterEdit_->setPlaceholderText(tr("Filter artists and albums…"));
    filterEdit_->setClearButtonEnabled(true);
    connect(filterEdit_, &QLineEdit::textChanged, this, [this] {
        // Локальный фильтр дешёв, но пересобирать прокси на каждую букву незачем.
        filterTimer_->start(kFilterDebounceMs);
    });
    topBar->addWidget(filterEdit_, 1);
    lay->addLayout(topBar);

    splitter_ = new QSplitter(Qt::Horizontal, this);
    lay->addWidget(splitter_, 1);

    // ── Левая панель: дерево групп и альбомов ───────────────────────────
    tree_->setObjectName(QStringLiteral("databaseTree"));
    treeProxy_->setSourceModel(model_);
    tree_->setModel(treeProxy_);
    tree_->setUniformRowHeights(true);
    // Вертикальный скролл — всегда навырост.
    tree_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    tree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // На темах с оверлей-скроллами даже AlwaysOn рисуется поверх viewport.
    // Два уровня защиты: стильшит на скроллбаре (QSS переключает Qt на свою
    // отрисовку, без оверлея) плюс viewportMargins, резервирующий место.
    tree_->verticalScrollBar()->setStyleSheet(
        QStringLiteral("QScrollBar:vertical { width: 12px; }"));
    tree_->setScrollbarMargin(12);
    // Двойной клик занят добавлением в очередь; раскрывают стрелку и Right/Left.
    tree_->setExpandsOnDoubleClick(false);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    tree_->header()->resizeSection(1, 120);
    // Третья колонка — распорка, отодвигающая правую границу колонки
    // счётчиков левее полосы скролла. Без неё оверлей-скроллбар перекрывает
    // край секции, и ресайз последней колонки невозможен.
    tree_->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    tree_->header()->resizeSection(2, 12);
    tree_->setSortingEnabled(true);
    tree_->sortByColumn(0, Qt::AscendingOrder);
    treeProxy_->setSortRole(MusicDbRoles::SortRole);

    // Раскрытие грузит альбомы явно: вид не всегда успевает спросить
    // canFetchMore до отрисовки, и узел оставался пустым.
    connect(tree_, &QTreeView::expanded, this, [this](const QModelIndex &idx) {
        if (treeProxy_->canFetchMore(idx))
            treeProxy_->fetchMore(idx);
    });
    connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex &cur) { showNode(cur); });
    splitter_->addWidget(tree_);

    // ── Правая панель: шапка с обложкой и таблица трэков ───────────────
    auto *right = new QWidget(this);
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);

    auto *header = new QHBoxLayout;
    cover_ = new CoverWidget(right);
    cover_->setCoverSize(96);
    cover_->installEventFilter(this);
    cover_->setCursor(Qt::PointingHandCursor);
    header->addWidget(cover_, 0, Qt::AlignTop);

    auto *texts = new QVBoxLayout;
    headerTitle_ = new QLabel(right);
    headerTitle_->setObjectName(QStringLiteral("databaseHeaderTitle"));
    headerTitle_->setWordWrap(true);
    QFont titleFont = headerTitle_->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.15);
    headerTitle_->setFont(titleFont);

    headerSubtitle_ = new QLabel(right);
    headerSubtitle_->setObjectName(QStringLiteral("databaseHeaderArtist"));
    headerStats_ = new QLabel(right);
    headerStats_->setObjectName(QStringLiteral("databaseHeaderStats"));

    texts->addWidget(headerTitle_);
    texts->addWidget(headerSubtitle_);
    texts->addWidget(headerStats_);
    texts->addStretch();
    header->addLayout(texts, 1);

    auto *headBox = new QWidget(right);
    headBox->setLayout(header);
    rl->addWidget(headBox);

    trackFilter_->setObjectName(QStringLiteral("databaseTrackFilter"));
    trackFilter_->setPlaceholderText(tr("Filter tracks…"));
    trackFilter_->setClearButtonEnabled(true);
    connect(trackFilter_, &QLineEdit::textChanged, this,
            [this](const QString &text) {
                // Правый фильтр — по уже загруженному списку: сервер не нужен.
                trackProxy_->setFilterFixedString(text.trimmed());
            });
    rl->addWidget(trackFilter_);

    trackTable_->setObjectName(QStringLiteral("databaseTracks"));
    trackProxy_->setSourceModel(trackModel_);
    trackProxy_->setSortRole(DatabaseTrackModel::SortRole);
    trackProxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    trackProxy_->setFilterKeyColumn(-1);   // ищем по всем колонкам
    trackTable_->setModel(trackProxy_);
    trackTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    trackTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    trackTable_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    trackTable_->verticalScrollBar()->setStyleSheet(
        QStringLiteral("QScrollBar:vertical { width: 12px; }"));
    trackTable_->setScrollbarMargin(12);
    trackTable_->setSortingEnabled(true);
    trackTable_->sortByColumn(DatabaseTrackModel::TitleColumn,
                              Qt::AscendingOrder);

    auto *trackHeader = trackTable_->horizontalHeader();
    // Подписи по левому краю: центрирование читается как пустая колонка.
    trackHeader->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    trackHeader->setStretchLastSection(false);
    trackHeader->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(trackHeader, &QWidget::customContextMenuRequested,
            this, &DatabaseBrowser::showHeaderMenu);
    connect(trackHeader, &QHeaderView::sectionResized,
            this, [this](int logicalIndex, int, int newSize) {
        if (applyingColumns_)
            return;
        Config::instance().setDatabaseColumnWidth(
            DatabaseTrackModel::columnKey(logicalIndex), newSize);
    });
    applyColumnConfig();

    rl->addWidget(trackTable_, 1);

    // Шапка без выбранного узла не врёт пустыми подписями.
    headerTitle_->setText(tr("Select an artist or album"));
    splitter_->addWidget(right);
    splitter_->setStretchFactor(0, 2);
    splitter_->setStretchFactor(1, 3);

    filterTimer_->setSingleShot(true);
    connect(filterTimer_, &QTimer::timeout, this, &DatabaseBrowser::applyTreeFilter);

    // Состав группировок — только поддерживаемые сервером теги.
    const QVector<TagChoice> choices = {
        {MPD_TAG_ARTIST,      tr("Artist")},
        {MPD_TAG_ALBUM_ARTIST,tr("Album artist")},
        {MPD_TAG_GENRE,       tr("Genre")},
        {MPD_TAG_DATE,        tr("Year")},
        {MPD_TAG_COMPOSER,    tr("Composer")},
    };
    const QVector<enum mpd_tag_type> supported =
        cmds_ ? cmds_->supportedTags() : QVector<enum mpd_tag_type>{};
    for (const TagChoice &choice : choices) {
        if (!supported.isEmpty() && !supported.contains(choice.tag))
            continue;
        tagCombo_->addItem(choice.label, static_cast<int>(choice.tag));
    }
    if (tagCombo_->count() == 0)
        tagCombo_->addItem(tr("Artist"), MPD_TAG_ARTIST);
}

// ── Намерения и активация ────────────────────────────────────────────────

QVector<MpdTagConstraint> DatabaseBrowser::activationFor(
    const QModelIndex &index) const
{
    if (!index.isValid())
        return {};
    // Дерево показывает прокси: модель обязана получить свой индекс.
    return model_->activationFor(treeProxy_->mapToSource(index));
}

QVector<BrowserIntent> DatabaseBrowser::selectedIntents() const
{
    QVector<BrowserIntent> intents;
    for (const QModelIndex &idx : uniqueSelectedRows(tree_)) {
        // Разбор строки один и тот же, что у двойного клика: меню не имеет права
        // предлагать то, чего активация сделать не может.
        const MusicDbRef ref = model_->refFor(treeProxy_->mapToSource(idx));
        if (!ref.valid)
            continue;
        QVector<MpdTagConstraint> constraints =
            musicDbConstraints(model_->groupTag(), ref.type,
                               ref.name, ref.groupName);
        if (!constraints.isEmpty())
            intents.append(BrowserIntent{BrowserIntent::Kind::TagGroup, {}, constraints});
    }
    return intents;
}

QVector<BrowserIntent> DatabaseBrowser::trackIntents() const
{
    QVector<BrowserIntent> intents;
    for (const QModelIndex &idx : uniqueSelectedRows(trackTable_)) {
        const QString uri = idx.data(DatabaseTrackModel::UriRole).toString();
        if (!uri.isEmpty())
            intents.append(BrowserIntent{BrowserIntent::Kind::Song, uri, {}});
    }
    return intents;
}

void DatabaseBrowser::setMetadataManager(MetadataManager *mgr)
{
    cover_->setMetadataManager(mgr);
}

// ── Правая панель ─────────────────────────────────────────────────────────

void DatabaseBrowser::showNode(const QModelIndex &proxyIndex)
{
    const MusicDbRef ref = model_->refFor(treeProxy_->mapToSource(proxyIndex));
    if (!ref.valid) {
        cachedGroupKey_.clear();
        cachedSongs_.clear();
        trackModel_->clear();
        setHeaderInfo({}, {}, -1, 0, nullptr);
        return;
    }

    const QVector<MpdTagConstraint> constraints =
        musicDbConstraints(model_->groupTag(), ref.type,
                           ref.name, ref.groupName);

    // Альбом той же группы берём из кэша: сервер уже отдал всю группу.
    if (ref.isAlbum() && !cachedGroupKey_.isEmpty()
        && cachedGroupKey_ == ref.groupName) {
        QVector<DbSong> subset;
        for (const DbSong &s : cachedSongs_) {
            if (QString::compare(s.album, ref.name) == 0)
                subset.append(s);
        }
        trackModel_->setTracks(subset);
    } else {
        loadTracksFor(ref.groupName, ref.isAlbum() ? ref.name : QString(),
                       constraints);
    }

    const DbSong *coverFrom = nullptr;
    for (int row = 0; row < trackModel_->rowCount(); ++row) {
        if (!trackModel_->trackAt(row)->album.isEmpty()) {
            coverFrom = trackModel_->trackAt(row);
            break;
        }
    }
    if (!coverFrom && !cachedSongs_.isEmpty())
        coverFrom = &cachedSongs_.first();

    // Если полноэкранный просмотр был закрыт — освободить память.
    if (coverViewer_ && !coverViewer_->isVisible())
        coverViewer_->clear();

    setHeaderInfo(musicDbDisplayName(ref.name),
                  ref.isAlbum() ? musicDbDisplayName(ref.groupName)
                                : tr("Group"),
                  ref.songs, ref.playTimeSec, coverFrom);
}

void DatabaseBrowser::loadTracksFor(const QString &group,
                                     const QString &album,
                                     const QVector<MpdTagConstraint> &constraints)
{
    if (!conn_ || !cmds_ || !conn_->isConnected()) {
        trackModel_->clear();
        return;
    }

    // Группа читается целиком один раз: переключение её альбомов не должно
    // повторять запрос, а счётчики сервера уже точны.
    if (album.isEmpty()) {
        cachedGroupKey_ = group;
        cachedSongs_ = cmds_->songsForConstraints(constraints);
        trackModel_->setTracks(cachedSongs_);
        return;
    }

    // Альбом без кэша группы — отдельный запрос с двумя ограничениями.
    cachedGroupKey_.clear();
    cachedSongs_.clear();
    trackModel_->setTracks(cmds_->songsForConstraints(constraints));
}

// ── Шапка ─────────────────────────────────────────────────────────────────

void DatabaseBrowser::setHeaderInfo(const QString &title, const QString &subtitle,
                                    int tracks, quint64 seconds,
                                    const DbSong *coverFrom)
{
    headerTitle_->setText(title.isEmpty() ? tr("Select an artist or album") : title);
    headerSubtitle_->setText(subtitle);

    if (tracks >= 0) {
        const QString span = compactSpan(seconds);
        headerStats_->setText(
            span.isEmpty()
                ? tr("%n track(s)", "", tracks)
                : tr("%n track(s) · %1", "", tracks).arg(span));
    } else {
        headerStats_->clear();
    }

    if (coverFrom && !coverFrom->album.isEmpty()) {
        cover_->fetchCover(coverFrom->artist, coverFrom->album,
                           coverFrom->uri);
    } else {
        cover_->clear();
    }
}

// ── Колонки правой таблицы ───────────────────────────────────────────────

void DatabaseBrowser::applyColumnConfig()
{
    applyingColumns_ = true;
    auto &cfg = Config::instance();
    QHeaderView *header = trackTable_->horizontalHeader();

    for (int column = 0; column < DatabaseTrackModel::ColumnCount; ++column) {
        const QString key = DatabaseTrackModel::columnKey(column);
        const bool saved = cfg.hasDatabaseColumnSetting(key);
        // Основной набор виден, пока пользователь явно не скрыл колонку.
        const bool core =
            column == DatabaseTrackModel::TitleColumn
            || column == DatabaseTrackModel::TimeColumn;
        header->setSectionHidden(column, saved ? !cfg.databaseColumnVisible(key)
                                               : !core);
    }

    header->setSectionResizeMode(DatabaseTrackModel::TitleColumn,
                                 QHeaderView::Stretch);
    for (const int c : {DatabaseTrackModel::TimeColumn,
                        DatabaseTrackModel::TrackColumn}) {
        if (!header->isSectionHidden(c))
            header->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    }

    // Восстановить сохранённые ширины: они переключают колонку на Interactive.
    for (int column = 0; column < DatabaseTrackModel::ColumnCount; ++column) {
        if (column == DatabaseTrackModel::TitleColumn)
            continue;   // Stretch не хранит ширину
        const QString key = DatabaseTrackModel::columnKey(column);
        const int w = cfg.databaseColumnWidth(key, -1);
        if (w > 0 && !header->isSectionHidden(column)) {
            header->setSectionResizeMode(column, QHeaderView::Interactive);
            header->resizeSection(column, w);
        }
    }

    int target = 0;
    for (const QString &key : cfg.databaseColumnsOrder()) {
        const int column = DatabaseTrackModel::columnForKey(key);
        if (column < 0)
            continue;
        header->moveSection(header->visualIndex(column), target++);
    }
    applyingColumns_ = false;
}

void DatabaseBrowser::showHeaderMenu(const QPoint &pos)
{
    auto *header = trackTable_->horizontalHeader();
    QMenu menu(trackTable_);

    for (int column = 0; column < DatabaseTrackModel::ColumnCount; ++column) {
        const QString title =
            trackModel_->headerData(column, Qt::Horizontal).toString();
        if (title.isEmpty())
            continue;
        QAction *act = menu.addAction(title);
        act->setCheckable(true);
        act->setChecked(!header->isSectionHidden(column));
        connect(act, &QAction::triggered, this, [this, column](bool on) {
            Config::instance().setDatabaseColumnVisible(
                DatabaseTrackModel::columnKey(column), on);
            applyColumnConfig();
        });
    }

    menu.addSeparator();
    QAction *edit = menu.addAction(tr("Edit Columns…"));
    connect(edit, &QAction::triggered, this, &DatabaseBrowser::editColumns);

    menu.exec(header->mapToGlobal(pos));
}

void DatabaseBrowser::editColumns()
{
    auto *header = trackTable_->horizontalHeader();

    // Порядок берём с экрана: пользователь мог тасовать столбцы мышью.
    QVector<QPair<int, int>> byVisual;
    for (int column = 0; column < DatabaseTrackModel::ColumnCount; ++column)
        byVisual.append(qMakePair(header->visualIndex(column), column));
    std::sort(byVisual.begin(), byVisual.end());

    QVector<ColumnChoice> choices;
    for (const auto &pair : byVisual) {
        ColumnChoice choice;
        choice.key     = DatabaseTrackModel::columnKey(pair.second);
        choice.title   =
            trackModel_->headerData(pair.second, Qt::Horizontal).toString();
        choice.visible = !header->isSectionHidden(pair.second);
        choices.append(choice);
    }

    ColumnChooserDialog dlg(this);
    dlg.setColumns(choices);
    if (dlg.exec() != QDialog::Accepted)
        return;

    auto &cfg = Config::instance();
    QStringList order;
    for (const ColumnChoice &choice : dlg.columns()) {
        cfg.setDatabaseColumnVisible(choice.key, choice.visible);
        order.append(choice.key);   // порядок окна — он и есть новый
    }
    cfg.setDatabaseColumnsOrder(order);
    applyColumnConfig();
}

// ── Фильтр дерева ─────────────────────────────────────────────────────────

void DatabaseBrowser::applyTreeFilter()
{
    const QString text = filterEdit_->text().trimmed();
    treeProxy_->setFilterFixedString(text);

    if (text.isEmpty()) {
        tree_->collapseAll();
        return;
    }
    // Совпавшие группы раскрываем: иначе фильтр по альбому ничего не покажет.
    for (int row = 0; row < treeProxy_->rowCount(); ++row)
        tree_->expand(treeProxy_->index(row, 0));
}

// ── Клик по обложке: полноэкранный просмотр ──────────────────────────────

bool DatabaseBrowser::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == cover_ && event->type() == QEvent::MouseButtonPress) {
        if (!coverViewer_)
            coverViewer_ = new CoverViewer;
        coverViewer_->show(cover_->coverData());
        return true;
    }
    return BrowserWidget::eventFilter(obj, event);
}

// ── Смена группировки и соединения ───────────────────────────────────────

void DatabaseBrowser::onTagChanged(int index)
{
    const auto tag = static_cast<enum mpd_tag_type>(
        tagCombo_->itemData(index).toInt());

    cachedGroupKey_.clear();
    cachedSongs_.clear();
    trackModel_->clear();

    if (!conn_ || !conn_->isConnected())
        return;   // выбор переживёт переподключение

    model_->loadGroups(tag);
}

void DatabaseBrowser::onActivate()
{
    const auto tag = static_cast<enum mpd_tag_type>(
        tagCombo_->currentData().toInt());
    if (conn_ && conn_->isConnected() && model_->rowCount({}) == 0)
        model_->loadGroups(tag);
}

void DatabaseBrowser::onConnectionChanged(bool connected)
{
    setEnabled(connected);

    cachedGroupKey_.clear();
    cachedSongs_.clear();

    const auto tag = static_cast<enum mpd_tag_type>(
        tagCombo_->currentData().toInt());
    if (connected && conn_ && cmds_) {
        model_->loadGroups(tag);
    } else {
        trackModel_->clear();
        setHeaderInfo({}, {}, -1, 0, nullptr);
    }
}

void DatabaseBrowser::onDeactivate()
{
    // Закрываем полноэкранный просмотр, если открыт.
    if (coverViewer_) {
        coverViewer_->clear();
        coverViewer_->close();
    }

    if (!Config::instance().lowMemoryMode())
        return;

    cachedGroupKey_.clear();
    cachedSongs_.clear();
    model_->clear();
    trackModel_->clear();
}