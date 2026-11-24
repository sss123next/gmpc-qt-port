#pragma once

/**
 * @file file-browser.h
 * @brief MPD file-system browser (2-pane: tree + song list).
 *
 * Left pane: directory tree (only folders, filtered via proxy).
 * Right pane: songs/searchable content of selected directory.
 * Uses FileSystemModel with lazy loading (canFetchMore/fetchMore).
 */

#include "browser-widget.h"
#include "browser-intent.h"

#include <QSet>
#include <QSortFilterProxyModel>

class Connection;
class FileSystemModel;
class QTreeView;
class QLineEdit;

/// Прокси для левого дерева: только директории + фильтр по имени.
class DirectoryFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit DirectoryFilterProxy(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) {
        setFilterCaseSensitivity(Qt::CaseInsensitive);
        setFilterKeyColumn(0);
    }
protected:
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex &sourceParent) const override;
};

/// Browse MPD's virtual filesystem.
class FileBrowser : public BrowserWidget {
    Q_OBJECT
public:
    explicit FileBrowser(Connection *conn, QWidget *parent = nullptr);
    ~FileBrowser() override;

    QString title() const override { return tr("Files"); }
    QString iconName() const override { return QStringLiteral("folder"); }

    /// The embedded tree view (for context menu setup).
    QTreeView *treeView() const { return leftView_; }

    /** @brief Правая панель — содержимое выбранного каталога.
     *  Индексы из неё проксированные: `enterDirectory()` принимает именно их. */
    QTreeView *contentView() const { return rightView_; }

    /** @brief Перейти в каталог, выбрав его в левом дереве (двойной клик справа).
     *  Как в оригинале: двойной клик по каталогу не добавляет его в очередь, а открывает.
     */
    void enterDirectory(const QModelIndex &contentIndex);

    /** @brief Намерения выделенных строк правой панели: каталог, трэк или список.
     *  Индексы проксированные, но UriRole/TypeRole прокси отдаёт исходной моделью, так
     *  что маппинг здесь нужен только если когда-нибудь понадобится сам узел.
     */
    [[nodiscard]] QVector<BrowserIntent> contentIntents() const;

    /// Намерения выделенных строк левого дерева каталогов (те же типы, та же разметка).
    [[nodiscard]] QVector<BrowserIntent> treeIntents() const;

    void onActivate() override;
    void onDeactivate() override;
    void onConnectionChanged(bool connected) override;

private slots:
    void onLeftSelectionChanged();
    void onTreeFilterChanged(const QString &text);
    void onContentFilterChanged(const QString &text);

private:
    Connection *conn_;
    FileSystemModel *model_;
    DirectoryFilterProxy *dirProxy_;    ///< фильтр: только директории
    QSortFilterProxyModel *searchProxy_;///< поиск по имени в правой панели
    QTreeView *leftView_;               ///< directory tree
    QTreeView *rightView_;              ///< song list for selected directory
    QLineEdit *treeFilterEdit_;         ///< фильтр левого дерева
    QLineEdit *contentFilterEdit_;      ///< фильтр правой панели
};

