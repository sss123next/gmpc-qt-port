#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

/**
 * @file test-add-url-dialog.cpp
 * @brief Окно «Add URL»: поле адреса и гаснущая на пустоте кнопка Add.
 *
 * Проверяется само окно, без транспорта — оно о сервере ничего не знает:
 * отдаёт лишь строку адреса, а команду шлёт вызвавший (MainWindow).
 */

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include "gui/add-url-dialog.h"

namespace {

/// Кнопка окна по её objectName.
QPushButton *buttonNamed(QWidget *w, const char *name)
{
    auto *btn = w->findChild<QPushButton *>(QLatin1String(name));
    REQUIRE(btn != nullptr);
    return btn;
}

} // namespace

TEST_CASE("окно откривается с гасной кнопкой Add")
{
    AddUrlDialog dlg;

    CHECK(dlg.url().isEmpty());
    CHECK_FALSE(buttonNamed(&dlg, "addBtn")->isEnabled());
}

TEST_CASE("ввод адреса вклучает Add и убирает пробелы по краям")
{
    AddUrlDialog dlg;
    auto *entry = dlg.findChild<QLineEdit *>(QStringLiteral("urlEntry"));
    REQUIRE(entry != nullptr);
    QPushButton *add = buttonNamed(&dlg, "addBtn");

    // Хвостовый '\n' — каприз Qt: поле отдаёт текст как есть, а адресом он не является.
    entry->setText(QStringLiteral("  http://radio.example/stream.mp3 \n"));
    CHECK(add->isEnabled());
    // Адрес уходит без пробелов по краям: серверу они не часть пути.
    CHECK_EQ(dlg.url(), "http://radio.example/stream.mp3");

    entry->setText(QString());   // пусто
    CHECK_FALSE(add->isEnabled());

    // Одни пробелы адресом не считаются — отдавать серверу нечего.
    entry->setText("   ");
    CHECK_FALSE(add->isEnabled());
}

TEST_CASE("accept закрывает окно и оставляет введённый адрес")
{
    AddUrlDialog dlg;
    auto *entry = dlg.findChild<QLineEdit *>(QStringLiteral("urlEntry"));
    REQUIRE(entry != nullptr);
    entry->setText(QStringLiteral("gmpc-qt-tmp"));   // имя сохранённого списка

    buttonNamed(&dlg, "addBtn")->click();

    CHECK_EQ(dlg.result(), int(QDialog::Accepted));
    CHECK_EQ(dlg.url(), "gmpc-qt-tmp");
}

TEST_CASE("Close отвергает окно и не трогает поле")
{
    AddUrlDialog dlg;
    auto *entry = dlg.findChild<QLineEdit *>(QStringLiteral("urlEntry"));
    REQUIRE(entry != nullptr);
    entry->setText(QStringLiteral("http://radio.example/stream.mp3"));

    buttonNamed(&dlg, "closeBtn")->click();

    CHECK_EQ(dlg.result(), int(QDialog::Rejected));
    // Адрес остаётся в поле: окно можно открыть заново и не набирать его снова.
    CHECK_EQ(entry->text().trimmed(), "http://radio.example/stream.mp3");
}

TEST_CASE("setUrl предзаполняет поле и вклучает Add")
{
    AddUrlDialog dlg;
    QPushButton *add = buttonNamed(&dlg, "addBtn");
    CHECK_FALSE(add->isEnabled());

    // Напримр, из буфера обмена: вызов ставит текст как есть.
    dlg.setUrl(QStringLiteral("http://radio.example/stream.mp3"));

    CHECK(add->isEnabled());
    CHECK_EQ(dlg.url(), "http://radio.example/stream.mp3");
}

int main(int argc, char *argv[])
{
    doctest::Context ctx(argc, argv);
    QApplication app(argc, argv);   // окно живёт на стеке до статиков Qt
    return ctx.run();
}