#pragma once

/**
 * @file column-chooser-dialog.h
 * @brief Выбор видимых колонок очереди и их порядка.
 *
 * Повторяет `egg_column_chooser_dialog` оригинала: список колонок с флажками и
 * перемещение строк вверх/вниз. Окно ничего не знает ни о виде, ни о Config —
 * оно лишь отдаёт новый порядок с видимостью, а применяет его вызвавший
 * (CurrentPlaylist), которому же принадлежит и запись настройки.
 */

#include <QDialog>
#include <QVector>

class QListWidget;
class QPushButton;

/// Одна колонка в списке выбора: ключ для хранения и подпись, как её видит пользователь.
struct ColumnChoice {
    QString key;         ///< PlaylistModel::columnKey()
    QString title;       ///< заголовок столбца
    bool visible = true;
};

/// Модальное окно «Edit columns»: флажки + порядок строк.
class ColumnChooserDialog : public QDialog {
    Q_OBJECT
public:
    explicit ColumnChooserDialog(QWidget *parent = nullptr);

    /// Заполнить список в текущем порядке и выставить отметки видимости.
    void setColumns(const QVector<ColumnChoice> &columns);
    /** @brief Текущий порядок с видимостью; после accept() — новое состояние вида. */
    const QVector<ColumnChoice> &columns() const { return columns_; }

private:
    /// Сдвинуть выбранную строку на delta (вверх/вниз).
    void moveCurrent(int delta);
    /// Перерисовать список из columns_ и встать на указанную строку.
    void rebuildList(int selectRow = -1);
    /// Доступность кнопок перемещения по текущей позиции выделения.
    void updateButtons();

    QVector<ColumnChoice> columns_;
    QListWidget *list_;
    QPushButton *upBtn_;
    QPushButton *downBtn_;
};