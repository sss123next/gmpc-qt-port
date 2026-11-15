#pragma once

/**
 * @file add-url-dialog.h
 * @brief Окно «Add URL» — поле адреса и кнопки Close / Add.
 *
 * Повторяет `gmpc-add-url.ui` оригинала: пока в поле ничено нет, Add гаснет —
 * отдавать серверу нечего. Окно ничего не знает о транспорте и об очереди:
 * оно лишь возвращает адрес, а команду шлёт вызвавший (MainWindow).
 */

#include <QDialog>

class QLineEdit;
class QPushButton;

/// Модальное окно ввода URL для добывления его в очередь.
class AddUrlDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddUrlDialog(QWidget *parent = nullptr);

    /// Предзаполнить поле (напримр, выбранным текстом из буфера обмена).
    void setUrl(const QString &url);
    /** @brief Адрес без хвостовых пробелов; пусто — пользователь ничено не ввёл.
     *
     *  Add при этом и так гаснет, поэтому вызвавший может проверять лишь
     *  result() окна, а строку брать уже после Accepted.
     */
    const QString url() const;

private:
    /// Доступность Add по содержимому поля: пустой адрес отдавать нечего.
    void updateAddButton();

    QLineEdit *entry_;
    QPushButton *closeBtn_;
    QPushButton *addBtn_;
};