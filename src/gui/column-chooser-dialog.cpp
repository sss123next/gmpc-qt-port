/**
 * @file column-chooser-dialog.cpp
 * @brief Реализация окна выбора колонок очереди.
 */

#include "column-chooser-dialog.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>   // std::swap

namespace {

/// Ключ колонки лежит в данных строки: при перестановке он едет вместе с ней.
constexpr int kKeyRole = Qt::UserRole + 1;

} // namespace

ColumnChooserDialog::ColumnChooserDialog(QWidget *parent)
    : QDialog(parent), list_(new QListWidget(this))
{
    setWindowTitle(tr("Edit columns"));

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(tr("Columns shown in the queue, in this order:"), this));

    list_->setObjectName(QStringLiteral("columnList"));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    // Перестановка только кнопками: drag&drop в списке Qt сбивает порядок флажков
    // при неочевидных жестах, а кнопки видны и предсказуемы.
    lay->addWidget(list_, 1);

    auto *row = new QHBoxLayout;
    upBtn_   = new QPushButton(tr("Move up"), this);
    downBtn_ = new QPushButton(tr("Move down"), this);
    upBtn_->setObjectName(QStringLiteral("moveUpBtn"));
    downBtn_->setObjectName(QStringLiteral("moveDownBtn"));
    row->addWidget(upBtn_);
    row->addWidget(downBtn_);
    row->addStretch();
    lay->addLayout(row);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &ColumnChooserDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &ColumnChooserDialog::reject);
    lay->addWidget(box);

    connect(upBtn_,   &QPushButton::clicked, this, [this] { moveCurrent(-1); });
    connect(downBtn_, &QPushButton::clicked, this, [this] { moveCurrent(+1); });

    // Флажок — это и есть видимость колонки; держим columns_ в согласии с экраном.
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        const QString key = item->data(kKeyRole).toString();
        for (ColumnChoice &choice : columns_) {
            if (choice.key == key)
                choice.visible = item->checkState() == Qt::Checked;
        }
    });
    connect(list_, &QListWidget::currentRowChanged,
            this, [this] { updateButtons(); });

    updateButtons();
}

void ColumnChooserDialog::setColumns(const QVector<ColumnChoice> &columns)
{
    columns_ = columns;
    rebuildList(0);
}

void ColumnChooserDialog::rebuildList(int selectRow)
{
    // Сигнал itemChanged во время перезаполнения не должен считаться правкой:
    // значения видимости уже лежат в columns_.
    const QSignalBlocker blocker(list_);
    list_->clear();
    for (const ColumnChoice &choice : columns_) {
        auto *item = new QListWidgetItem(choice.title, list_);
        item->setData(kKeyRole, choice.key);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(choice.visible ? Qt::Checked : Qt::Unchecked);
    }

    if (selectRow >= 0 && selectRow < list_->count())
        list_->setCurrentRow(selectRow);

    updateButtons();
}

void ColumnChooserDialog::moveCurrent(int delta)
{
    const int row = list_->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= columns_.size())
        return;

    std::swap(columns_[row], columns_[target]);
    rebuildList(target);
}

void ColumnChooserDialog::updateButtons()
{
    const int row = list_->currentRow();
    upBtn_->setEnabled(row > 0 && !columns_.isEmpty());
    downBtn_->setEnabled(row >= 0 && row < columns_.size() - 1);
}