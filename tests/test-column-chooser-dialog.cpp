#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

/**
 * @file test-column-chooser-dialog.cpp
 * @brief Окно выбора колонок очереди: порядок строк и отметки видимости.
 *
 * Проверяется само окно, без вида и Config — оно о них ничего не знает:
 * список заполняется в переданном порядке, перемещение меняет его же,
 * а снятая галочка уезжает в результат. Модального exec() здесь нет —
 * вызывается accept(), как это делал бы пользователь.
 */

#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>

#include "gui/column-chooser-dialog.h"

namespace {

/// Три колонки в исходном порядке — больше для перестановок и не нужно.
QVector<ColumnChoice> threeColumns()
{
    return { ColumnChoice{QStringLiteral("title"),  QStringLiteral("Title")},
             ColumnChoice{QStringLiteral("artist"), QStringLiteral("Artist")},
             ColumnChoice{QStringLiteral("album"),  QStringLiteral("Album")} };
}

/// Ключи в текущем порядке окна.
QStringList keysOf(const ColumnChooserDialog &dlg)
{
    QStringList keys;
    for (const ColumnChoice &choice : dlg.columns())
        keys.append(choice.key);
    return keys;
}

QPushButton *buttonNamed(QWidget *w, const char *name)
{
    auto *btn = w->findChild<QPushButton *>(QLatin1String(name));
    REQUIRE(btn != nullptr);
    return btn;
}

} // namespace

TEST_CASE("окно показывает колонки в переданном порядке с их отметками")
{
    ColumnChooserDialog dlg;
    QVector<ColumnChoice> columns = threeColumns();
    columns[2].visible = false;   // Album скрыт
    dlg.setColumns(columns);

    auto *list = dlg.findChild<QListWidget *>(QStringLiteral("columnList"));
    REQUIRE(list != nullptr);
    REQUIRE_EQ(list->count(), 3);
    CHECK_EQ(list->item(0)->text(), QStringLiteral("Title"));
    CHECK_EQ(list->item(1)->text(), QStringLiteral("Artist"));
    CHECK_EQ(list->item(2)->checkState(), Qt::Unchecked);   // скрытая — без галочки
}

TEST_CASE("перемещение вниз меняет порядок и упирается в край")
{
    ColumnChooserDialog dlg;
    dlg.setColumns(threeColumns());

    auto *list = dlg.findChild<QListWidget *>(QStringLiteral("columnList"));
    REQUIRE(list != nullptr);
    QPushButton *down = buttonNamed(&dlg, "moveDownBtn");

    list->setCurrentRow(0);   // Title
    CHECK(down->isEnabled());
    down->click();

    CHECK_EQ(keysOf(dlg), (QStringList{QStringLiteral("artist"), QStringLiteral("title"),
                                       QStringLiteral("album")}));
    // Строка уехала вместе с выделением: следующий шаг вниз возможен.
    CHECK_EQ(list->currentRow(), 1);

    down->click();
    CHECK_EQ(keysOf(dlg), (QStringList{QStringLiteral("artist"), QStringLiteral("album"),
                                       QStringLiteral("title")}));
    // Ниже некуда — кнопка гаснет, повторный клик ничего не портит.
    CHECK_FALSE(down->isEnabled());
    down->click();
    CHECK_EQ(keysOf(dlg), (QStringList{QStringLiteral("artist"), QStringLiteral("album"),
                                       QStringLiteral("title")}));
}

TEST_CASE("перемещение вверх с первой строки выключено")
{
    ColumnChooserDialog dlg;
    dlg.setColumns(threeColumns());

    auto *list = dlg.findChild<QListWidget *>(QStringLiteral("columnList"));
    REQUIRE(list != nullptr);
    QPushButton *up = buttonNamed(&dlg, "moveUpBtn");

    list->setCurrentRow(0);
    CHECK_FALSE(up->isEnabled());

    up->click();   // клик по выключенной кнопке порядок не трогает
    CHECK_EQ(keysOf(dlg), (QStringList{QStringLiteral("title"), QStringLiteral("artist"),
                                       QStringLiteral("album")}));

    list->setCurrentRow(1);
    up->click();
    CHECK_EQ(keysOf(dlg), (QStringList{QStringLiteral("artist"), QStringLiteral("title"),
                                       QStringLiteral("album")}));
}

TEST_CASE("снятая галочка попадает в результат окна")
{
    ColumnChooserDialog dlg;
    dlg.setColumns(threeColumns());

    auto *list = dlg.findChild<QListWidget *>(QStringLiteral("columnList"));
    REQUIRE(list != nullptr);
    list->item(1)->setCheckState(Qt::Unchecked);   // Artist скрыт

    CHECK_FALSE(dlg.columns().at(1).visible);

    // exec() без пользователя повис бы: закрываем окно сами.
    dlg.accept();
    CHECK_EQ(dlg.result(), int(QDialog::Accepted));
}

TEST_CASE("окно без колонок не падает и кнопки гаснут")
{
    ColumnChooserDialog dlg;
    auto *list = dlg.findChild<QListWidget *>(QStringLiteral("columnList"));
    REQUIRE(list != nullptr);

    CHECK_EQ(list->count(), 0);
    CHECK_FALSE(buttonNamed(&dlg, "moveUpBtn")->isEnabled());
    CHECK_FALSE(buttonNamed(&dlg, "moveDownBtn")->isEnabled());

    dlg.setColumns({});
    CHECK_EQ(dlg.columns().size(), 0);
}

int main(int argc, char *argv[])
{
    doctest::Context ctx(argc, argv);
    QApplication app(argc, argv);   // окно живёт на стеке до статиков Qt
    return ctx.run();
}