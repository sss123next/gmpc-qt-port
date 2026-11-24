#pragma once

/**
 * @file music-database-model.h
 * @brief Дерево базы MPD: группы верхнего уровня → альбомы, со статистикой.
 *
 * Трэки в дереве больше не живут — их показывает правая панель DatabaseBrowser
 * (`DatabaseTrackModel`). Здесь остались два уровня навигации; объёмы групп и
 * альбомов приходят серверным `count … group`, поэтому счётчик виден до того,
 * как узел раскрыт, а не после чтения сотни строк.
 */

#include <QAbstractItemModel>
#include <QString>
#include <QList>

#include <mpd/tag.h>

#include "core/mpd-commands.h"

/// Роли данных дерева для вида и меню.
namespace MusicDbRoles {
    /// Полная подпись объёма («12 · 48 m») — колонка статистики.
    constexpr int StatsRole = Qt::UserRole + 1;
}

/// Узел дерева базы: группа верхнего уровня либо альбом внутри неё.
struct MusicDbItem {
    /// Group — значение тега группировки (артист/жанр/год…), Album — альбом группы.
    enum Type { Group, Album };

    Type type;
    QString name;            ///< сырое значение тега (пустое — «без тега»)
    int songs = -1;          ///< число трэков (−1 — сервер не сказал)
    quint64 playTimeSec = 0; ///< суммарная длительность
    QList<MusicDbItem *> children;
    MusicDbItem *parent = nullptr;
    bool loaded = false;     ///< детей запрашивали (пустой альбом — тоже «загружен»)

    ~MusicDbItem() { qDeleteAll(children); }
};

/** @brief Ограничения `findadd` для строки дерева.
 *
 *  Группа → одно ограничение по тегу группировки; альбом добавляет к нему
 *  собственное имя. Пустое значение группы — легальный фильтр «без тега».
 */
[[nodiscard]] QVector<MpdTagConstraint> musicDbConstraints(
    enum mpd_tag_type rootTag, MusicDbItem::Type type,
    const QString &name, const QString &groupName);

/** @brief Разбор строки дерева для правой панели и меню.
 *
 *  Сырые значения (пустая группа — легальный фильтр) отдельно от подписей:
 *  по ним строятся ограничения, а «(Unknown)» остаётся только на экране.
 */
struct MusicDbRef {
    bool valid = false;
    MusicDbItem::Type type = MusicDbItem::Group;
    QString name;        ///< сырое имя узла
    QString groupName;   ///< сырое имя родительской группы (для альбома)
    int songs = -1;
    quint64 playTimeSec = 0;

    [[nodiscard]] bool isAlbum() const { return type == MusicDbItem::Album; }
};

/** @brief Компактная подпись объёма: «45 s», «12 m», «2 h 5 m»; ноль — пусто. */
[[nodiscard]] QString compactSpan(quint64 seconds);

/// Отображаемое имя значения тега: пустое → «(Unknown)».
[[nodiscard]] QString musicDbDisplayName(const QString &raw);

/** @brief Дерево базы MPD с ленивой загрузкой альбомов.
 *
 *  Группы грузит DatabaseBrowser; детей — canFetchMore()/fetchMore() в момент
 *  раскрытия. Вне соединения модель молчит и прежнее содержимое не теряет.
 */
class MusicDatabaseModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit MusicDatabaseModel(Connection *conn, MpdCommands *cmds,
                                QObject *parent = nullptr);
    ~MusicDatabaseModel() override;

    // ── Наполнение ───────────────────────────────────────────────────────
    /** @brief Загрузить группы верхнего уровня по тегу (артисты — частный случай).
     *
     *  Ответ сервера забирается целиком до сброса модели: временная ошибка не
     *  должна оставлять пользователя перед пустым экраном.
     */
    void loadGroups(enum mpd_tag_type tag);

    /// Выгрузить всё содержимое (режим низкой памяти).
    void clear();

    /// Тег группировки верхнего уровня — нужен ограничениям `findadd`.
    enum mpd_tag_type groupTag() const { return rootTag_; }

    /** @brief Что делать при активации строки дерева. */
    [[nodiscard]] QVector<MpdTagConstraint> activationFor(const QModelIndex &index) const;

    /// Сырые данные узла для правой панели (invalid — строка не наша).
    [[nodiscard]] MusicDbRef refFor(const QModelIndex &index) const;

    /// Загрузить альбомы группы (зовётся из fetchMore).
    void loadAlbums(const QModelIndex &idx);

    // ── QAbstractItemModel ───────────────────────────────────────────────
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    /// Группа без загруженных альбомов всё равно «с детьми»: иначе вид не
    /// рисует стрелку раскрытия и даже не спрашивает canFetchMore().
    bool hasChildren(const QModelIndex &parent) const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

private:
    Connection *conn_;
    MpdCommands *cmds_;
    MusicDbItem *root_;                          ///< невидимый корень
    enum mpd_tag_type rootTag_ = MPD_TAG_ARTIST;///< тег групп верхнего уровня
};