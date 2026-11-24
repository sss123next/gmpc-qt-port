#pragma once

/**
 * @file database-browser.h
 * @brief Database: дерево групп/альбомов слева, таблица трэков справа.
 *
 * Master-detail вместо одного глубокого дерева. Слева — навигация с фильтром и
 * объёмами узлов; справа — всегда видимый список песен выбранного узла, обложка
 * и шапка альбома. Действия отдаются наружу теми же `BrowserIntent`, что у
 * остальных браузеров: меню и двойной клик собирает MainWindow.
 */

#include <QSortFilterProxyModel>
#include <QVector>

#include "browser-widget.h"
#include "browser-intent.h"
#include "database-track-model.h"
#include "music-database-model.h"

class Connection;
class MpdCommands;
class MetadataManager;
class CoverWidget;
class QComboBox;
class QLabel;
class QLineEdit;
class QSortFilterProxyModel;
class QSplitter;
class QTableView;
class QTimer;
class QTreeView;

/// Фильтр дерева по именам групп и альбомов (родитель остаётся видимым).
class DatabaseTreeProxy : public QSortFilterProxyModel {
public:
    explicit DatabaseTreeProxy(QObject *parent = nullptr);

public:
    /// Проброс ленивой загрузки альбомов в модель-источник.
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
};

/// Просмотр базы MPD: группы → альбомы слева, трэки выбранного узла справа.
class DatabaseBrowser : public BrowserWidget {
    Q_OBJECT
public:
    explicit DatabaseBrowser(Connection *conn, MpdCommands *cmds,
                             QWidget *parent = nullptr);
    ~DatabaseBrowser() override;

    QString title() const override { return tr("Database"); }
    QString iconName() const override { return QStringLiteral("gmpc-database"); }

    /// Дерево навигации (для контекстного меню и тестов).
    QTreeView *treeView() const { return tree_; }
    /// Таблица трэков правой панели.
    QTableView *trackView() const { return trackTable_; }

    /** @brief Что делать при активации строки дерева (двойной клик). */
    [[nodiscard]] QVector<MpdTagConstraint> activationFor(const QModelIndex &index) const;

    /// Намерения выделенных узлов дерева для контекстного меню.
    [[nodiscard]] QVector<BrowserIntent> selectedIntents() const;
    /// Намерения выделенных трэков правой панели.
    [[nodiscard]] QVector<BrowserIntent> trackIntents() const;

    /// Обложки и метаданные шапки берутся из общего менеджера MainWindow.
    void setMetadataManager(MetadataManager *mgr);

    void onActivate() override;
    void onDeactivate() override;
    void onConnectionChanged(bool connected) override;

private slots:
    void onTagChanged(int index);
    /// Выбор узла: загрузить его трэки и шапку (или очистить правую панель).
    void showNode(const QModelIndex &proxyIndex);
    /// Дебаунс локального фильтра дерева.
    void applyTreeFilter();
    /// Контекстное меню заголовка таблицы: видимость колонок и выбор порядка.
    void showHeaderMenu(const QPoint &pos);

private:
    void setupUi();
    /// Загрузить трэки под ограничениями узла в правую панель.
    void loadTracksFor(const QString &group, const QString &album,
                       const QVector<MpdTagConstraint> &constraints);
    /// Обложка и подписи шапки; пустые значения очищают её.
    void setHeaderInfo(const QString &title, const QString &subtitle,
                       int tracks, quint64 seconds, const DbSong *coverFrom);
    /// Применить сохранённые видимость/порядок колонок правой таблицы.
    void applyColumnConfig();
    void editColumns();

    Connection *conn_;
    MpdCommands *cmds_;

    MusicDatabaseModel *model_;          ///< дерево групп и альбомов
    DatabaseTreeProxy *treeProxy_;       ///< фильтр по именам
    QTreeView *tree_;
    QComboBox *tagCombo_;
    QLineEdit *filterEdit_;
    QTimer *filterTimer_;

    DatabaseTrackModel *trackModel_;
    QSortFilterProxyModel *trackProxy_;  ///< текстовый фильтр правой панели
    QTableView *trackTable_;
    QLineEdit *trackFilter_;

    CoverWidget *cover_;
    QLabel *headerTitle_;
    QLabel *headerSubtitle_;
    QLabel *headerStats_;
    QSplitter *splitter_;

    /// Кэш трэков выбранной группы: альбомы переключаются без повторного запроса.
    QString cachedGroupKey_;
    QVector<DbSong> cachedSongs_;
    bool applyingColumns_ = false;
};