#pragma once

/**
 * @file file-system-model.h
 * @brief QAbstractItemModel for MPD file-system browsing.
 *
 * Lazily loads directory contents via mpd_send_list_meta().
 * Supports three entity types: directory, song, and playlist.
 */

#include <QAbstractItemModel>
#include <QStringList>
#include <QVector>

class Connection;

/** @brief Отображаемое имя для пути, возвращённого MPD: последний компонент.
 *
 *  lsinfo отдаёт относительные к music_directory пути целиком
 *  («humor/karcev»), поэтому узел дерева хранит путь ровно как его вернул MPD, а
 *  показывает basename — как g_path_get_basename() в оригинальном gmpc.
 *  Хвостовые «/» в имя не входят; путь из одних слэшей даёт пустую строку.
 */
[[nodiscard]] QString mpdBasename(const QString &path);

/// A node in the MPD file-system tree.
struct FileNode {
    enum Type { Directory, Song, Playlist, Root };
    Type type;
    QString name;
    QString path;     ///< путь относительно music_directory, как его вернул MPD
    FileNode *parent = nullptr;
    QVector<FileNode *> children;
    unsigned duration = 0; ///< seconds (only for Song)
    bool loaded = false; ///< children fetched from MPD yet

    ~FileNode() { qDeleteAll(children); }
};

/// File-system tree model backed by MPD listmeta commands.
class FileSystemModel : public QAbstractItemModel {
    Q_OBJECT
public:
    /// Custom data role that returns the full virtual path.
    static constexpr int UriRole = Qt::UserRole + 1;

    /** @brief Роль типа узла: значение FileNode::Type как int (Qt::UserRole + 2).
     *
     *  Нужен активации двойным кликом: каталог в неё навигирует, трэк играется,
     *  список воспроизведения дописывается в очередь. По одному URI эти строки не
     *  различить — путь каталога и путь трэка выглядят одинаково.
     */
    static constexpr int TypeRole = Qt::UserRole + 2;
    explicit FileSystemModel(Connection *conn, QObject *parent = nullptr);
    ~FileSystemModel() override;

    // -- QAbstractItemModel interface ---------------------------------------
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orient,
                        int role = Qt::DisplayRole) const override;

    // -- Lazy loading ------------------------------------------------------
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

    /// Reload from root.
    void refresh();
    /// Unload all data and free memory.
    void clear();

private:
    void loadDirectory(FileNode *node);

    Connection *conn_;
    FileNode *root_;
};
