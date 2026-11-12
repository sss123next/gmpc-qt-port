#pragma once

/**
 * @file server-info-dialog.h
 * @brief Окно «Server Information»: снимок `stats` и время по значениям тега.
 *
 * Отклонение от оригинала (там окно модальное): наше обновляется само, пока открыто,
 * а модальность не давала бы работать с деревьями под ним. Значения аптайма и времени
 * игры тикают таймером — событие idle о них не приходит; таблица «Tag statistics»
 * пересобирается только когда база действительно изменилась (idle database|update) или
 * сменился тег: у MPD нет агрегата по всем значениям сразу, и полный обход базы в тысячи
 * строк не должен повторяться на каждом тике.
 */

#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QTableWidget>

#include "core/mpd-commands.h"

class QTimer;

/// Немодальное окно статистики сервера; закрывается само (WA_DeleteOnClose).
class ServerInfoDialog : public QDialog {
    Q_OBJECT
public:
    /** @param cmds  Транспорт для чтения `stats`; nullptr — значения подаются снаружи
     *               через setStats()/setTagRows(). */
    explicit ServerInfoDialog(MpdCommands *cmds, QWidget *parent = nullptr);

    /// Заполнить поля готовым снимком (тесты и живой вызов).
    void setStats(const ServerStats &stats);
    /// Строки таблицы «Tag statistics» в том виде, как их отрисует окно.
    [[nodiscard]] const QVector<TagPlaytime> &tagRows() const { return tagRows_; }

    /** @brief Отсортировать строки по времени убывания (чистая функция — для тестов).
     *  Равные значения держат порядок сервера: обход базы идёт алфавитно, и «прыгать»
     *  строкам на каждом перечитывании незачем.
     */
    static void sortTagRows(QVector<TagPlaytime> &rows);

public slots:
    /// Перечитать `stats`; вне соединения — показать заглушку и очистить поля.
    void refresh();
    /** @brief Начать пересборку таблицы по тегам: значения берутся у сервера, обход
     *  базы идёт порциями в тактах событийного цикла. */
    void reloadTagStats();
    /// Показать состояние «нет соединения» вместо значений (публичен — зовут извне).
    void showDisconnected();

private:
    /// Собрать раскладку окна (секции Server / Database / Tag statistics).
    void setupUi();
    /// Отсортировать накопленное и вывести в таблицу; вызывается по окончании обхода.
    void fillTable();
    /// Одна порция обхода базы: уложиться в бюджет времени, остаток — следующим тактом.
    void stepTagStats();

    MpdCommands *cmds_ = nullptr;
    QTimer *timer_ = nullptr;

    // Секции Server / Database
    QLabel *version_;
    QLabel *uptime_;
    QLabel *playtime_;
    QLabel *dbPlaytime_;
    QLabel *artists_;
    QLabel *albums_;
    QLabel *songs_;
    QLabel *urlHandlers_;

    // Секция Tag statistics
    QComboBox *tagCombo_;
    QTableWidget *table_;
    QLabel *scanHint_;            ///< «Scanned N of M…» либо итог обхода
    QVector<TagPlaytime> tagRows_; ///< накопленное время по значениям текущего тега
    QStringList pendingValues_;   ///< значения текущего тега, ещё не обойдённые
    int scanIndex_ = 0;
    enum mpd_tag_type currentTag_ = MPD_TAG_UNKNOWN;

    // Состояние
    QLabel *hint_;                ///< «нет соединения»
};