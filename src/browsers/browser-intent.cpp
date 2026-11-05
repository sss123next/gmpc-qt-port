/**
 * @file browser-intent.cpp
 * @brief Разбор намерений строки браузера: состав меню и локальные пути.
 */

#include "browser-intent.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QMenu>

namespace {

/// Каталог-родитель POSIX-пути строго синтаксически: stat() здесь не при чём.
QString parentFolder(const QString &path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    if (slash > 0)
        return path.left(slash);
    if (slash == 0)
        return QStringLiteral("/");
    return {};
}

} // namespace

QString localPathForMpdUri(const QString &mpdUri, const QString &musicDirectory)
{
    if (mpdUri.isEmpty() || musicDirectory.isEmpty())
        return {};

    const QString root = QDir::cleanPath(musicDirectory);
    QString rel = mpdUri;
    // lsinfo отдаёт пути относительно каталога музыки без ведущего слэша; если он всё
    // же пришёл, это не абсолютный путь, а шум. Точка — сам корень, строки дерева в
    // меню для него нет.
    while (rel.startsWith(QLatin1Char('/')))
        rel.remove(0, 1);
    if (rel.isEmpty() || rel == QLatin1String("."))
        return {};

    const QString abs = QDir::cleanPath(root + QLatin1Char('/') + rel);
    // cleanPath уже развернул «..», поэтому побег из каталога музыки виден как
    // отсутствие префикса. URI приходят с сервера, а по этому пути потом откроют
    // файловый менеджер — пускать его наружу нельзя.
    const QString prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    if (!abs.startsWith(prefix))
        return {};

    return abs;
}

BrowserMenuActions menuActionsFor(const QVector<BrowserIntent> &intents,
                                 const QString &musicDirectory)
{
    BrowserMenuActions actions;

    for (const BrowserIntent &intent : intents) {
        if (intent.kind == BrowserIntent::Kind::TagGroup) {
            // findadd без фильтра — «всю базу в очередь», поэтому группе нужны
            // именно ограничения; пустые ограничения добавлять нечего.
            if (!intent.constraints.isEmpty())
                actions.canAdd = true;
            continue;
        }

        if (intent.uri.isEmpty())
            continue;

        actions.canAdd = true;
        actions.canCopyPath = true;
        // Играть можно ровно один трэк: «сыграть выделенное» для десятка строк — это
        // уже замена очереди, а не воспроизведение.
        if (intent.kind == BrowserIntent::Kind::Song && intents.size() == 1)
            actions.canPlay = true;
    }

    actions.canReplace = actions.canAdd;
    actions.canOpenFolder = !folderToOpen(intents, musicDirectory).isEmpty();

    return actions;
}

QString folderToOpen(const QVector<BrowserIntent> &intents, const QString &musicDirectory)
{
    for (const BrowserIntent &intent : intents) {
        if (intent.kind != BrowserIntent::Kind::Song
            && intent.kind != BrowserIntent::Kind::Directory)
            continue;

        const QString path = localPathForMpdUri(intent.uri, musicDirectory);
        if (path.isEmpty())
            continue;

        return intent.kind == BrowserIntent::Kind::Directory ? path : parentFolder(path);
    }
    return {};
}

QString copyTextFor(const QVector<BrowserIntent> &intents, const QString &musicDirectory)
{
    QStringList lines;
    for (const BrowserIntent &intent : intents) {
        if (intent.kind == BrowserIntent::Kind::TagGroup || intent.uri.isEmpty())
            continue;

        const QString local = localPathForMpdUri(intent.uri, musicDirectory);
        lines << (local.isEmpty() ? intent.uri : local);
    }
    return lines.join(QLatin1Char('\n'));
}

BrowserPathMenuItems addPathMenuItems(QMenu &menu,
                                      const QVector<BrowserIntent> &intents,
                                      const QString &musicDirectory)
{
    BrowserPathMenuItems items;

    const QString folder = folderToOpen(intents, musicDirectory);
    const bool hereLocally = !folder.isEmpty() && QFileInfo(folder).isDir();

    items.openFolder = menu.addAction(
        QCoreApplication::translate("BrowserIntents", "Open Folder"));
    items.openFolder->setEnabled(hereLocally);
    if (!hereLocally) {
        // Отказ виден только из настроек: сервер может быть и удалённым, тогда путь
        // здесь вообще ничего не значит.
        items.openFolder->setToolTip(musicDirectory.isEmpty()
            ? QCoreApplication::translate("BrowserIntents",
                "Set the music directory in Preferences: MPD reports paths relative to it.")
            : QCoreApplication::translate("BrowserIntents",
                "%1 is not a folder on this machine.").arg(folder));
    } else {
        items.folder = folder;
    }

    items.copyPath = menu.addAction(
        QCoreApplication::translate("BrowserIntents", "Copy Path"));
    items.copyPath->setEnabled(!copyTextFor(intents, musicDirectory).isEmpty());

    return items;
}
