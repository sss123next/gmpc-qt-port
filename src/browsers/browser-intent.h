#pragma once

/**
 * @file browser-intent.h
 * @brief Что меню и двойной клик умеют делать со строкой любого дерева браузеров.
 *
 * Строки всех четырёх деревьев приводятся к одному виду, иначе на каждый браузер
 * пришлось бы своя ветка включений в каждом пункте меню. Разбор путей чистый (без MPD
 * и без виджетов): состав_menu_ проверяется юнитами, а существование каталога смотрит
 * уже построитель меню — ему и знать, локален ли сервер.
 */

#include <QString>
#include <QVector>

#include "core/mpd-commands.h"

class QAction;
class QMenu;

/// Одна строка браузера в виде, пригодном для команды MPD.
struct BrowserIntent {
    /** @brief Что строка собой представляет.
     *  Directory — каталог (`add` принимает каталог целиком), StoredPlaylist — файл
     *  списка (`load`, не `addid`: у списка нет URI трэка), TagGroup — узел таг-
     *  браузера, у которого файла нет вовсе, только ограничения поиска.
     */
    enum class Kind { Song, Directory, StoredPlaylist, TagGroup };

    Kind kind = Kind::Song;
    QString uri;                            ///< путь относительно music_directory
    QVector<MpdTagConstraint> constraints;   ///< только TagGroup: фильтры `findadd`
};

/// Активность пунктов контекстного меню для набора строк.
struct BrowserMenuActions {
    bool canPlay = false;        ///< ровно один трэк: играть его, очередь не трогая
    bool canAdd = false;         ///< есть что добавить (трэк, каталог, список или группа)
    bool canReplace = false;     ///< то же, но с очисткой очереди и последующей игрой
    bool canOpenFolder = false;  ///< путь разрешается в локальный каталог
    bool canCopyPath = false;    ///< есть строка с URI, который можно показать текстом
};

/** @brief Состав меню для выделения.
 *  @param musicDirectory  значение настройки «Music directory»; пустое — «открыть
 *         папку» недоступно, потому что MPD оперирует путями относительно своего
 *         каталога музыки и абсолютный путь без него не восстановить.
 *
 *  canOpenFolder говорит лишь о том, что путь вычислился; существует ли он на самом
 *  деле — решение вызывающей стороны (сервер может быть удалённым).
 */
[[nodiscard]] BrowserMenuActions menuActionsFor(
    const QVector<BrowserIntent> &intents, const QString &musicDirectory);

/** @brief Локальный путь строки базы; пусто, если восстановить нельзя.
 *  @param mpdUri          URI ровно как его вернул MPD (относительно music_directory)
 *  @param musicDirectory  абсолютный каталог музыки из настроек
 *
 *  Путь, который после нормализации уходит из music_directory («../…»), считается
 *  невалидным: URI приходят с сервера, а меню потом откроет этот путь в файловом
 *  менеджере.
 */
[[nodiscard]] QString localPathForMpdUri(const QString &mpdUri,
                                        const QString &musicDirectory);

/** @brief Каталог для «Открыть папку»; пусто, если ни одна строка не даёт каталог.
 *  Берётся первая добавляемая строка с URI: при выделении в десять трэков открывать
 *  десять окон никто не просил.
 */
[[nodiscard]] QString folderToOpen(const QVector<BrowserIntent> &intents,
                                  const QString &musicDirectory);

/** @brief Текст для «Копировать путь»: по строке на выделенную позицию.
 *  Абсолютный локальный путь, когда каталог музыки задан, иначе URI так, как его
 *  зовёт MPD — `mpc add` понимает именно его.
 */
[[nodiscard]] QString copyTextFor(const QVector<BrowserIntent> &intents,
                                 const QString &musicDirectory);

/// Пункты «Открыть папку»/«Копировать путь», добавленные в меню.
struct BrowserPathMenuItems {
    QAction *openFolder = nullptr;
    QAction *copyPath = nullptr;
    QString folder;   ///< каталог для открытия; пусто, если открывать нечего или его нет
};

/** @brief Добавляет в меню «Open Folder» и «Copy Path» с уже посчитанными включениями.
 *  @param musicDirectory  значение настройки «Music directory» из Preferences
 *
 *  Единственный смысл двух пунктов один для всех деревьев, а причина отказа — только
 *  из настроек; держать эту ветку в каждом браузере означало бы три разных текста
 *  подсказки про одну и ту же ситуацию.
 */
BrowserPathMenuItems addPathMenuItems(QMenu &menu,
                                      const QVector<BrowserIntent> &intents,
                                      const QString &musicDirectory);
