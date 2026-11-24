/**
 * @file file-browser.cpp
 * @brief MPD filesystem browser — 2-pane: directory tree + song list.
 */

#include "file-browser.h"
#include "file-system-model.h"
#include "core/connection.h"
#include "core/config.h"
#include <QHeaderView>
#include <QLineEdit>
#include <QSplitter>
#include <QTreeView>
#include <QVBoxLayout>

// ── DirectoryFilterProxy ───────────────────────────────────────────────────

bool DirectoryFilterProxy::filterAcceptsRow(int sourceRow,
                                            const QModelIndex &sourceParent) const
{
    // Сначала текстовый фильтр (по DisplayRole колонки 0).
    if (!QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent))
        return false;

    // Потом — только директории.
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!idx.isValid())
        return false;
    return idx.data(FileSystemModel::TypeRole).toInt() == FileNode::Directory;
}

// ── FileBrowser ─────────────────────────────────────────────────────────────

namespace {

/// Ключи колонок правой панели для Config::fileBrowserColumnWidth.
const char *colKeys[] = {"name", "type", "duration"};

/** @brief Намерение строки файловой модели.
 *  @returns false, если активировать строку нечем (Root — сам корень дерева).
 */
bool intentForFileIndex(const QModelIndex &idx, BrowserIntent *out)
{
    const QString uri = idx.data(FileSystemModel::UriRole).toString();
    if (uri.isEmpty())
        return false;

    switch (static_cast<FileNode::Type>(idx.data(FileSystemModel::TypeRole).toInt())) {
    case FileNode::Song:
        out->kind = BrowserIntent::Kind::Song;
        break;
    case FileNode::Directory:
        out->kind = BrowserIntent::Kind::Directory;
        break;
    case FileNode::Playlist:
        // Файл .m3u — сохранённый список: ему `load`, а не `addid` (замерено в 7.3).
        out->kind = BrowserIntent::Kind::StoredPlaylist;
        break;
    default:
        return false;
    }

    out->uri = uri;
    return true;
}

QVector<BrowserIntent> intentsOfRows(const QList<QModelIndex> &rows)
{
    QVector<BrowserIntent> intents;
    for (const QModelIndex &idx : rows) {
        BrowserIntent intent;
        if (intentForFileIndex(idx, &intent))
            intents.append(intent);
    }
    return intents;
}

} // namespace

FileBrowser::FileBrowser(Connection *conn, QWidget *parent)
    : BrowserWidget(parent)
    , conn_(conn)
    , model_(new FileSystemModel(conn, this))
    , dirProxy_(new DirectoryFilterProxy(this))
    , searchProxy_(new QSortFilterProxyModel(this))
    , leftView_(new QTreeView(this))
    , rightView_(new QTreeView(this))
    , treeFilterEdit_(new QLineEdit(this))
    , contentFilterEdit_(new QLineEdit(this))
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    // ── Proxies ───────────────────────────────────────────────────────
    dirProxy_->setSourceModel(model_);

    searchProxy_->setSourceModel(model_);
    searchProxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    searchProxy_->setFilterKeyColumn(0);   // ищем по имени

    auto *splitter = new QSplitter(Qt::Horizontal, this);

    // ── Left pane: directory tree ─────────────────────────────────────
    {
        auto *leftLay = new QVBoxLayout;
        leftLay->setContentsMargins(0, 0, 0, 0);
        leftLay->setSpacing(0);

        treeFilterEdit_->setPlaceholderText(tr("Filter directories…"));
        treeFilterEdit_->setClearButtonEnabled(true);
        connect(treeFilterEdit_, &QLineEdit::textChanged,
                this, &FileBrowser::onTreeFilterChanged);
        leftLay->addWidget(treeFilterEdit_);

        leftView_->setModel(dirProxy_);
        leftView_->setExpandsOnDoubleClick(true);
        leftView_->setSelectionMode(QAbstractItemView::SingleSelection);
        leftView_->setSelectionBehavior(QAbstractItemView::SelectRows);
        {
            auto *lh = leftView_->header();
            lh->setStretchLastSection(false);
            lh->setSectionResizeMode(0, QHeaderView::Interactive);
            lh->hideSection(1);  // Type
            lh->hideSection(2);  // Duration
            const int w = Config::instance().fileBrowserColumnWidth(QLatin1String("tree_name"), -1);
            if (w > 0)
                lh->resizeSection(0, w);
            connect(lh, &QHeaderView::sectionResized, this, [](int idx, int, int w) {
                if (idx == 0)
                    Config::instance().setFileBrowserColumnWidth(QLatin1String("tree_name"), w);
            });
        }
        leftLay->addWidget(leftView_, 1);

        // Обёртка для splitter: layout не может быть прямым child'ом splitter'a,
        // поэтому создаём QWidget-контейнер.
        auto *leftPane = new QWidget(this);
        leftPane->setLayout(leftLay);
        splitter->addWidget(leftPane);
    }

    // ── Right pane: content of selected directory ─────────────────────
    {
        auto *rightLay = new QVBoxLayout;
        rightLay->setContentsMargins(0, 0, 0, 0);
        rightLay->setSpacing(0);

        contentFilterEdit_->setPlaceholderText(tr("Filter files…"));
        contentFilterEdit_->setClearButtonEnabled(true);
        connect(contentFilterEdit_, &QLineEdit::textChanged,
                this, &FileBrowser::onContentFilterChanged);
        rightLay->addWidget(contentFilterEdit_);

        rightView_->setModel(searchProxy_);
        rightView_->setRootIsDecorated(false);
        rightView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
        rightView_->setSelectionBehavior(QAbstractItemView::SelectRows);
        rightView_->setSortingEnabled(true);
        // Ширины колонок восстанавливаются из Config и сохраняются при изменении.
        auto *rh = rightView_->header();
        rh->setStretchLastSection(false);
        for (int c = 0; c < 3; ++c)
            rh->setSectionResizeMode(c, QHeaderView::Interactive);
        auto &cfg = Config::instance();
        for (int c = 0; c < 3; ++c) {
            const int w = cfg.fileBrowserColumnWidth(QLatin1String(colKeys[c]), -1);
            if (w > 0)
                rh->resizeSection(c, w);
        }
        connect(rh, &QHeaderView::sectionResized, this, [this](int idx, int, int w) {
            if (idx >= 0 && idx < 3)
                Config::instance().setFileBrowserColumnWidth(QLatin1String(colKeys[idx]), w);
        });

        rightLay->addWidget(rightView_, 1);

        auto *rightPane = new QWidget(this);
        rightPane->setLayout(rightLay);
        splitter->addWidget(rightPane);
    }

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({250, 400});

    lay->addWidget(splitter);

    // Смена выбора в левом дереве обновляет содержимое правой панели.
    connect(leftView_->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this, &FileBrowser::onLeftSelectionChanged);
}

FileBrowser::~FileBrowser() = default;

void FileBrowser::onLeftSelectionChanged()
{
    // Индекс в dirProxy_ → source model → searchProxy_.
    const QModelIndex srcIdx = dirProxy_->mapToSource(leftView_->currentIndex());
    const QModelIndex proxyIdx = srcIdx.isValid()
        ? searchProxy_->mapFromSource(srcIdx)
        : QModelIndex();
    rightView_->setRootIndex(proxyIdx);

    if (!srcIdx.isValid())
        return;

    auto &cfg = Config::instance();

    // ── Правая панель: автофит, если ещё ни разу не ресайзили ────────
    bool hasRightWidths = false;
    for (int c = 0; c < 3 && !hasRightWidths; ++c)
        hasRightWidths = cfg.fileBrowserColumnWidth(QLatin1String(colKeys[c]), -1) > 0;
    if (!hasRightWidths)
        for (int c = 0; c < rightView_->header()->count(); ++c)
            rightView_->resizeColumnToContents(c);

    // ── Левое дерево: автофит колонки имени ──────────────────────────
    const int nameW = cfg.fileBrowserColumnWidth(QLatin1String("tree_name"), -1);
    if (nameW <= 0)
        leftView_->resizeColumnToContents(0);
}

void FileBrowser::onTreeFilterChanged(const QString &text)
{
    dirProxy_->setFilterFixedString(text);
}

void FileBrowser::onContentFilterChanged(const QString &text)
{
    searchProxy_->setFilterFixedString(text);
}

void FileBrowser::enterDirectory(const QModelIndex &contentIndex)
{
    if (!contentIndex.isValid())
        return;

    // contentIndex — прокси-индекс из searchProxy_. Маппим в source,
    // затем в dirProxy_, чтобы передать левому дереву.
    const QModelIndex srcIdx = searchProxy_->mapToSource(contentIndex);
    const QModelIndex dirIdx = dirProxy_->mapFromSource(srcIdx);
    leftView_->setCurrentIndex(dirIdx);
}

QVector<BrowserIntent> FileBrowser::contentIntents() const
{
    return intentsOfRows(uniqueSelectedRows(rightView_));
}

QVector<BrowserIntent> FileBrowser::treeIntents() const
{
    return intentsOfRows(uniqueSelectedRows(leftView_));
}

void FileBrowser::onActivate()
{
    if (conn_->isConnected() && model_->canFetchMore({}))
        model_->fetchMore({});
}

void FileBrowser::onDeactivate()
{
    if (Config::instance().lowMemoryMode())
        model_->clear();
}

void FileBrowser::onConnectionChanged(bool connected)
{
    setEnabled(connected);
    if (connected) {
        model_->refresh();
        model_->fetchMore({});
    }
}
