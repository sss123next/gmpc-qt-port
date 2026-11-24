#pragma once

/**
 * @file database-track-model.h
 * @brief Таблица трэков правой панели Database.
 *
 * Плоский список: узлы дерева (группа или альбом) отдают свои ограничения, а
 * строки приходят одним запросом `find` с окном. Сортировка — локальная и
 * числовая там, где это номера; порядок сервера без sort не определён.
 */

#include <QAbstractTableModel>
#include <QString>

#include "core/mpd-commands.h"

/// Таблица трэков выбранного узла базы.
class DatabaseTrackModel : public QAbstractTableModel {
    Q_OBJECT
public:
    /// Колонки; первые три — основной набор, остальные скрыты до включения.
    enum Column { TrackColumn = 0, TitleColumn, TimeColumn,
                  ArtistColumn, AlbumColumn, DiscColumn, ColumnCount };

    /// Роли вида и меню.
    enum Roles {
        UriRole = Qt::UserRole + 1,   ///< путь для команд очереди
        SortRole                      ///< числа сравниваются численно
    };

    explicit DatabaseTrackModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    /** @brief Сортировка по клику заголовка.
     *  Персистентные индексы пересчитываются вручную: сброс модели терял бы
     *  выделение на каждом щелчке, а трэк под курсором — то, что играют. */
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    /// Заменить содержимое ответом сервера.
    void setTracks(const QVector<DbSong> &tracks);
    void clear();

    /// Строка по номеру (nullptr вне диапазона).
    [[nodiscard]] const DbSong *trackAt(int row) const;

    /** @brief Ключ колонки для настроек видимости/порядка.
     *
     *  Именем, а не номером: пользователь переставляет столбцы, и настройка
     *  обязана остаться при своём — как у очереди (`playlistColumnKey`).
     */
    [[nodiscard]] static QString columnKey(int column);
    /// Колонка по ключу (-1, если ключа нет).
    [[nodiscard]] static int columnForKey(const QString &key);

private:
    QVector<DbSong> tracks_;
};