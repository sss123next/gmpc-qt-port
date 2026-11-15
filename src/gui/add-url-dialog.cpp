/**
 * @file add-url-dialog.cpp
 * @brief Реализация окна «Add URL».
 */

#include "add-url-dialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

AddUrlDialog::AddUrlDialog(QWidget *parent)
    : QDialog(parent), entry_(new QLineEdit(this))
{
    setWindowTitle(tr("Add URL"));

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(tr("Enter URL:"), this));

    // Оригинал ждал адреса схемы (`<scheme>://…`) или имени сохранённого списка;
    // подсказка говорит об этом прямо в поле, а не отдельной надписью.
    entry_->setObjectName(QStringLiteral("urlEntry"));
    entry_->setPlaceholderText(tr("http://\u2026 or a playlist name"));

    lay->addWidget(entry_);

    auto *row = new QHBoxLayout;
    row->addStretch();
    closeBtn_ = new QPushButton(tr("Close"), this);
    addBtn_   = new QPushButton(tr("Add"), this);
    closeBtn_->setObjectName(QStringLiteral("closeBtn"));
    addBtn_->setObjectName(QStringLiteral("addBtn"));
    row->addWidget(closeBtn_);
    row->addWidget(addBtn_);
    lay->addLayout(row);

    connect(entry_, &QLineEdit::textChanged, this,
            [this] { updateAddButton(); });
    // Enter в поле — то же, что клик по Add: иначе пришлось бы тянуться к мыше.
    connect(entry_, &QLineEdit::returnPressed, this, [this] {
        if (!url().isEmpty())
            accept();
    });

    connect(closeBtn_, &QPushButton::clicked, this, &AddUrlDialog::reject);
    // Гасная кнопка не должна закрывать окно пустым адресом: проверка та же,
    // что и у доступности — одно правило на два места.
    connect(addBtn_, &QPushButton::clicked, this, [this] {
        if (url().isEmpty())
            return;
        accept();
    });

    updateAddButton();
}

void AddUrlDialog::setUrl(const QString &url)
{
    entry_->setText(url);
    updateAddButton();
}

const QString AddUrlDialog::url() const
{
    // Хвостовый пробел в поле — это не часть адреса, а след курсора.
    return entry_->text().trimmed();
}

void AddUrlDialog::updateAddButton()
{
    addBtn_->setEnabled(!entry_->text().trimmed().isEmpty());
}