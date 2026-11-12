#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-server-info-dialog.cpp
 * @brief Тесты окна «Server Information»: подписи времени и таблица по тегам.
 *
 * Сервер в кейсах не нужен — MpdCommands подменён наследником с готовыми ответами
 * (методы транспорта виртуальны ровно для этого). Живой кейс включается
 * GMPC_QT_TEST_MPD и проверяет, что настоящие `stats`/`list <tag>` разбираются.
 */

#include <algorithm>

#include <QApplication>
#include <QComboBox>
#include <QHash>
#include <QLabel>
#include <QTableWidget>

#include "core/connection.h"
#include "core/mpd-commands.h"
#include "gui/server-info-dialog.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QApplication app(argc, &argv);

namespace {

/// Транспорт с подменёнными ответами: ни одного обращения к сети.
class FakeCmds : public MpdCommands {
public:
    FakeCmds() : MpdCommands(&conn_) {}

    std::optional<ServerStats> serverStats() const override { return stats; }
    QVector<enum mpd_tag_type> supportedTags() const override { return tags; }

    QStringList tagValues(enum mpd_tag_type) const override
    {
        // Порядок как у сервера: алфавитный — на нём проверяется устойчивость сортировки.
        QStringList sorted = values;
        std::sort(sorted.begin(), sorted.end());
        return sorted;
    }

    std::optional<TagPlaytime> tagPlaytime(enum mpd_tag_type, const QString &value) const override
    {
        auto it = playtimes.constFind(value);
        if (it == playtimes.constEnd())
            return std::nullopt;
        TagPlaytime row;
        row.value       = value;
        row.playTimeSec = *it;
        row.songs       = 1;
        return row;
    }

    Connection conn_;
    ServerStats stats;                    ///< что «отвечает» сервер на `stats`
    QVector<enum mpd_tag_type> tags{MPD_TAG_ARTIST};
    QStringList values{"Beta", "Alpha"};
    QHash<QString, quint64> playtimes{{"Alpha", 600}, {"Beta", 60}};
};

QLabel *label(ServerInfoDialog &dlg, const char *name)
{
    return dlg.findChild<QLabel *>(QString::fromLatin1(name));
}

} // namespace

TEST_CASE("подпись времени опускает нулевые компоненты и не врёт про ноль")
{
    CHECK_EQ(formatSpan(0), QStringLiteral("0 s"));
    CHECK_EQ(formatSpan(45), QStringLiteral("45 s"));
    CHECK_EQ(formatSpan(125), QStringLiteral("2 m 5 s"));
    // Ровный час — без «0 s» на хвосте: подпись должна читаться как есть.
    CHECK_EQ(formatSpan(3600), QStringLiteral("1 h"));
    CHECK_EQ(formatSpan(90061), QStringLiteral("1 d 1 h 1 m 1 s"));
}

TEST_CASE("снимок stats заполняет все поля окна")
{
    ServerInfoDialog dlg(nullptr);

    ServerStats stats;
    stats.version       = QStringLiteral("0.25.6");
    stats.uptimeSec     = 125;
    stats.playtimeSec   = 3600;
    stats.dbPlaytimeSec = 90061;
    stats.artists       = 7;
    stats.albums        = 9;
    stats.songs         = 42;
    dlg.setStats(stats);

    CHECK_EQ(label(dlg, "version")->text(), QStringLiteral("0.25.6"));
    CHECK_EQ(label(dlg, "uptime")->text(), QStringLiteral("2 m 5 s"));
    CHECK_EQ(label(dlg, "playtime")->text(), QStringLiteral("1 h"));
    CHECK_EQ(label(dlg, "dbPlaytime")->text(), QStringLiteral("1 d 1 h 1 m 1 s"));
    CHECK_EQ(label(dlg, "artists")->text(), QStringLiteral("7"));
    CHECK_EQ(label(dlg, "albums")->text(), QStringLiteral("9"));
    CHECK_EQ(label(dlg, "songs")->text(), QStringLiteral("42"));
    // Схем нет — пишем «N/A», а не пустую строку: так делал оригинал.
    CHECK_EQ(label(dlg, "urlHandlers")->text(), QStringLiteral("N/A"));
    CHECK(label(dlg, "hintLabel")->text().isEmpty());

    stats.urlHandlers = {QStringLiteral("http://"), QStringLiteral("https://")};
    dlg.setStats(stats);
    CHECK_EQ(label(dlg, "urlHandlers")->text(), QStringLiteral("http://, https://"));
}

TEST_CASE("без соединения окно говорит об этом и очищает значения")
{
    ServerInfoDialog dlg(nullptr);

    ServerStats stats;
    stats.version = QStringLiteral("0.25.6");
    stats.songs   = 3;
    dlg.setStats(stats);
    CHECK_FALSE(label(dlg, "songs")->text().isEmpty());

    QMetaObject::invokeMethod(&dlg, "showDisconnected", Qt::DirectConnection);

    CHECK_EQ(label(dlg, "hintLabel")->text(), QStringLiteral("Not connected"));
    CHECK(label(dlg, "version")->text().isEmpty());
    CHECK(label(dlg, "songs")->text().isEmpty());
}

TEST_CASE("refresh с транспортом без сервера показывает заглушку")
{
    FakeCmds cmds;
    ServerInfoDialog dlg(&cmds);

    // Транспорт отвечает nullopt — значит соединения нет.
    QMetaObject::invokeMethod(&dlg, "showDisconnected", Qt::DirectConnection);
    CHECK_EQ(label(dlg, "hintLabel")->text(), QStringLiteral("Not connected"));

    cmds.stats.version = QStringLiteral("0.25.6");
    dlg.refresh();
    CHECK(label(dlg, "hintLabel")->text().isEmpty());
    CHECK_EQ(label(dlg, "version")->text(), QStringLiteral("0.25.6"));
}

TEST_CASE("строки таблицы сортируются по времени убывания, равные не прыгают")
{
    QVector<TagPlaytime> rows;
    for (const auto &pair : {std::make_pair(QStringLiteral("A"), quint64(60)),
                             std::make_pair(QStringLiteral("B"), quint64(600)),
                             std::make_pair(QStringLiteral("C"), quint64(60))}) {
        TagPlaytime row;
        row.value       = pair.first;
        row.playTimeSec = pair.second;
        rows << row;
    }

    ServerInfoDialog::sortTagRows(rows);

    REQUIRE_EQ(rows.size(), 3);
    CHECK_EQ(rows[0].value, QStringLiteral("B"));
    // A и C равны: устойчивая сортировка обязана сохранить исходный порядок.
    CHECK_EQ(rows[1].value, QStringLiteral("A"));
    CHECK_EQ(rows[2].value, QStringLiteral("C"));
}

TEST_CASE("таблица по тегам собирается из ответов сервера с долей от максимума")
{
    FakeCmds cmds;
    ServerInfoDialog dlg(&cmds);

    QMetaObject::invokeMethod(&dlg, "reloadTagStats", Qt::DirectConnection);

    auto *table = dlg.findChild<QTableWidget *>(QStringLiteral("statsTable"));
    REQUIRE(table != nullptr);
    // Значений два — обход укладывается в один такт событийного цикла.
    REQUIRE_EQ(table->rowCount(), 2);

    CHECK_EQ(dlg.tagRows().size(), 2);
    CHECK_EQ(table->item(0, 0)->text(), QStringLiteral("Alpha"));
    CHECK_EQ(table->item(1, 0)->text(), QStringLiteral("Beta"));

    // Доля считается от самого длинного значения: у Alpha это все сто процентов.
    CHECK_EQ(table->item(0, 2)->text(), QStringLiteral("100 %"));
    CHECK_EQ(table->item(1, 2)->text(), QStringLiteral("10 %"));

    // Подпись обхода гасится по завершении; список тегов заполнен именами сервера.
    auto *combo = dlg.findChild<QComboBox *>(QStringLiteral("tagCombo"));
    REQUIRE(combo != nullptr);
    CHECK_EQ(combo->count(), 1);
    CHECK_FALSE(combo->itemText(0).isEmpty());
}

TEST_CASE("пустая база по тегу оставляет таблицу пустой без выдумок")
{
    FakeCmds cmds;
    cmds.values.clear();
    ServerInfoDialog dlg(&cmds);

    QMetaObject::invokeMethod(&dlg, "reloadTagStats", Qt::DirectConnection);

    auto *table = dlg.findChild<QTableWidget *>(QStringLiteral("statsTable"));
    REQUIRE(table != nullptr);
    CHECK_EQ(table->rowCount(), 0);
}

// ── Живой кейс: настоящие stats и list <tag> с одноразового сервера ─────────

TEST_CASE("живой сервер отдаёт статистику и значения тега")
{
    if (qEnvironmentVariableIsEmpty("GMPC_QT_TEST_MPD")) {
        MESSAGE("GMPC_QT_TEST_MPD не задан — интеграционная проверка пропущена");
        return;
    }

    Connection conn;
    REQUIRE(conn.connectToServer(qEnvironmentVariable("GMPC_QT_TEST_MPD"), 0, QString(), 5000));
    MpdCommands cmds(&conn);

    const auto stats = cmds.serverStats();
    REQUIRE(stats.has_value());
    CHECK_FALSE(stats->version.isEmpty());   // версия приходит из приветствия сервера
    CHECK_GE(stats->songs, 1);               // в служебной базе есть файлы

    const QVector<enum mpd_tag_type> tags = cmds.supportedTags();
    REQUIRE_FALSE(tags.isEmpty());
    if (tags.contains(MPD_TAG_ARTIST)) {
        const QStringList values = cmds.tagValues(MPD_TAG_ARTIST);
        REQUIRE_FALSE(values.isEmpty());     // Artist One из setup-скрипта
        CHECK(values.contains(QStringLiteral("Artist One")));

        const auto row = cmds.tagPlaytime(MPD_TAG_ARTIST, QStringLiteral("Artist One"));
        REQUIRE(row.has_value());
        CHECK_EQ(row->value, QStringLiteral("Artist One"));
    }

    conn.disconnect();
}