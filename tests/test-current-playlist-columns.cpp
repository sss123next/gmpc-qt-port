#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

/**
 * @file test-current-playlist-columns.cpp
 * @brief Вид очереди применяет сохранённый порядок и видимость колонок.
 *
 * Транспорт здесь настоящий, но сервер не нужен: раскладка столбцов — свойство
 * вида. Проверяются обе стороны настройки: применение при создании (Config →
 * вид) и запись выбора окна обратно в Config (`applyColumnChoices`).
 */

#include <QApplication>
#include <QHeaderView>
#include <QTemporaryDir>

#include "browsers/current-playlist.h"
#include "core/config.h"
#include "core/connection.h"
#include "core/mpd-commands.h"
#include "core/playlist-model.h"

namespace {

/// Настройка колонок глобальна (Config): кейс обязан вернуть её как было.
void resetColumnSettings()
{
    auto &cfg = Config::instance();
    for (int column = 0; column < PlaylistModel::ColumnCount; ++column)
        cfg.setPlaylistColumnVisible(playlistColumnKey(column), true);
    cfg.setPlaylistColumnsOrder({});
}

/// Вид над пустой моделью: очередь для раскладки не нужна.
struct ViewFixture {
    Connection conn;
    MpdCommands cmds{&conn};
    PlaylistModel model;
    CurrentPlaylist view{&conn, &cmds, &model};

    QHeaderView *header() const { return view.treeView()->horizontalHeader(); }
};

} // namespace

TEST_CASE("вид применяет сохранённый порядок и видимость колонок")
{
    resetColumnSettings();
    auto &cfg = Config::instance();
    cfg.setPlaylistColumnsOrder({QStringLiteral("time"), QStringLiteral("title")});
    cfg.setPlaylistColumnVisible(QStringLiteral("artist"), false);

    ViewFixture f;

    CHECK(f.header()->isSectionHidden(PlaylistModel::ArtistColumn));
    // Сохранённый порядок — это и есть визуальный: Time первым, Title за ним.
    CHECK_EQ(f.header()->visualIndex(PlaylistModel::TimeColumn), 0);
    CHECK_EQ(f.header()->visualIndex(PlaylistModel::TitleColumn), 1);

    resetColumnSettings();
}

TEST_CASE("applyColumnChoices записывает выбор и перестраивает вид")
{
    resetColumnSettings();

    ViewFixture f;
    // Все четыре колонки: порядок задан целиком, поэтому результат однозначен.
    const QVector<ColumnChoice> choices = {
        {QStringLiteral("album"),  QStringLiteral("Album")},
        {QStringLiteral("title"),  QStringLiteral("Title")},
        {QStringLiteral("artist"), QStringLiteral("Artist"), false},   // скрыт
        {QStringLiteral("time"),   QStringLiteral("Time")},
    };
    f.view.applyColumnChoices(choices);

    auto &cfg = Config::instance();
    CHECK_EQ(cfg.playlistColumnsOrder(),
             (QStringList{QStringLiteral("album"), QStringLiteral("title"),
                          QStringLiteral("artist"), QStringLiteral("time")}));
    CHECK_FALSE(cfg.playlistColumnVisible(QStringLiteral("artist")));
    // Скрытая колонка остаётся видимой для соседей: их порядок не поехал.
    CHECK(f.header()->isSectionHidden(PlaylistModel::ArtistColumn));
    CHECK_EQ(f.header()->visualIndex(PlaylistModel::AlbumColumn), 0);

    resetColumnSettings();
}

int main(int argc, char *argv[])
{
    // Свой каталог настроек на процесс: Config — синглтон над общим INI-файлом,
    // и соседние сьюты оставляли бы в нём чужие ключи колонок.
    QTemporaryDir configHome;
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());

    doctest::Context ctx(argc, argv);
    QApplication app(argc, argv);   // вид требует приложения; стек до статиков Qt
    return ctx.run();
}