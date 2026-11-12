/**
 * @file server-info-dialog.cpp
 * @brief Реализация окна «Server Information».
 *
 * Значения секций Server/Database приходят одним снимком `stats` (плюс список схем
 * URL), а таблица времени по тегам собирается обходом базы: у MPD нет агрегата
 * «playtime сразу по всем значениям», поэтому на каждое значение идёт своя команда
 * `count`. Обход разбит на порции — окно не должно замораживать GUI-поток.
 */

#include "server-info-dialog.h"

#include <algorithm>

#include <QElapsedTimer>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

namespace {

/// Метка-значение с именем — тесты ищут поля по objectName.
QLabel *valueLabel(const char *name)
{
    auto *label = new QLabel;
    label->setObjectName(QString::fromLatin1(name));
    return label;
}

constexpr int kRefreshMs = 2000;         ///< тик аптайма и времени игры
constexpr qint64 kScanBudgetMs = 20;     ///< сколько миллисекунд отдаём обходу за такт

} // namespace

ServerInfoDialog::ServerInfoDialog(MpdCommands *cmds, QWidget *parent)
    : QDialog(parent), cmds_(cmds)
{
    setWindowTitle(tr("Server Information"));
    setAttribute(Qt::WA_DeleteOnClose);
    setupUi();

    resize(420, 480);

    // Аптайм и время игры растут сами — события idle о них не приходит. Тик редкий:
    // секундная точность здесь никому не нужна.
    timer_ = new QTimer(this);
    timer_->setInterval(kRefreshMs);
    connect(timer_, &QTimer::timeout, this, &ServerInfoDialog::refresh);
    timer_->start();

    refresh();
}

void ServerInfoDialog::setupUi()
{
    auto *root = new QVBoxLayout(this);

    hint_ = valueLabel("hintLabel");
    root->addWidget(hint_);

    // ── Сервер: то, что меняется само по себе ───────────────────────────
    auto *serverForm = new QFormLayout;
    version_  = valueLabel("version");   serverForm->addRow(tr("Version"), version_);
    uptime_   = valueLabel("uptime");    serverForm->addRow(tr("Uptime"), uptime_);
    playtime_ = valueLabel("playtime");  serverForm->addRow(tr("Time playing"), playtime_);

    auto *serverBox = new QGroupBox(tr("Server"));
    serverBox->setLayout(serverForm);
    root->addWidget(serverBox);

    // ── База: размеры меняются только вместе с базой ────────────────────
    auto *dbForm = new QFormLayout;
    dbPlaytime_  = valueLabel("dbPlaytime");   dbForm->addRow(tr("Total playtime"), dbPlaytime_);
    artists_     = valueLabel("artists");      dbForm->addRow(tr("Number of artists"), artists_);
    albums_      = valueLabel("albums");       dbForm->addRow(tr("Number of albums"), albums_);
    songs_       = valueLabel("songs");        dbForm->addRow(tr("Number of songs"), songs_);
    urlHandlers_ = valueLabel("urlHandlers");  dbForm->addRow(tr("URL handlers"), urlHandlers_);

    auto *dbBox = new QGroupBox(tr("Database"));
    dbBox->setLayout(dbForm);
    root->addWidget(dbBox);

    // ── Время по значениям тега: тяжёлый обход, поэтому отдельной секцией
    auto *tagBox = new QGroupBox(tr("Tag statistics"));
    auto *tagLay = new QVBoxLayout(tagBox);

    tagCombo_ = new QComboBox;
    tagCombo_->setObjectName(QStringLiteral("tagCombo"));
    connect(tagCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { reloadTagStats(); });
    tagLay->addWidget(tagCombo_);

    scanHint_ = valueLabel("scanHint");
    tagLay->addWidget(scanHint_);

    table_ = new QTableWidget;
    table_->setObjectName(QStringLiteral("statsTable"));
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({tr("Value"), tr("Play time"), tr("Share")});
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tagLay->addWidget(table_);

    root->addWidget(tagBox, 1);
}

void ServerInfoDialog::sortTagRows(QVector<TagPlaytime> &rows)
{
    // std::stable_sort: равные длительности сохраняют порядок сервера (алфавитный).
    std::stable_sort(rows.begin(), rows.end(),
                     [](const TagPlaytime &a, const TagPlaytime &b) {
                         return a.playTimeSec > b.playTimeSec;
                     });
}

void ServerInfoDialog::setStats(const ServerStats &stats)
{
    hint_->clear();
    version_->setText(stats.version.isEmpty() ? QStringLiteral("—") : stats.version);
    uptime_->setText(formatSpan(stats.uptimeSec));
    playtime_->setText(formatSpan(stats.playtimeSec));
    dbPlaytime_->setText(formatSpan(stats.dbPlaytimeSec));
    artists_->setText(QString::number(stats.artists));
    albums_->setText(QString::number(stats.albums));
    songs_->setText(QString::number(stats.songs));
    // Пустой список — не «ноль схем», а молчание сервера: так и пишем, как оригинал.
    urlHandlers_->setText(stats.urlHandlers.isEmpty() ? tr("N/A")
                                                      : stats.urlHandlers.join(QStringLiteral(", ")));
}

void ServerInfoDialog::showDisconnected()
{
    hint_->setText(tr("Not connected"));
    version_->clear();
    uptime_->clear();
    playtime_->clear();
    dbPlaytime_->clear();
    artists_->clear();
    albums_->clear();
    songs_->clear();
    urlHandlers_->clear();

    tagRows_.clear();
    pendingValues_.clear();
    scanIndex_ = 0;
    table_->setRowCount(0);
}

void ServerInfoDialog::refresh()
{
    if (cmds_ == nullptr)
        return;   // значения подаются снаружи

    const auto stats = cmds_->serverStats();
    if (!stats) {
        showDisconnected();
        return;
    }
    setStats(*stats);
}

void ServerInfoDialog::reloadTagStats()
{
    tagRows_.clear();
    pendingValues_.clear();
    scanIndex_ = 0;
    table_->setRowCount(0);

    if (cmds_ == nullptr) {
        fillTable();
        return;
    }

    // Список тегов — у сервера. Пересобираем его при каждом заходе: соединение могло
    // смениться, а выбор пользователя сохраняем, если такой тег ещё включён.
    const QVector<enum mpd_tag_type> tags = cmds_->supportedTags();
    if (tags.isEmpty()) {
        scanHint_->setText(tr("No tag types"));
        return;
    }

    enum mpd_tag_type wanted = currentTag_;
    if (!tags.contains(wanted))
        wanted = tags.first();

    const QSignalBlocker block(tagCombo_);
    tagCombo_->clear();
    for (const enum mpd_tag_type tag : tags) {
        // Имя тега отдаёт сам libmpdclient — «Artist», а не номер: тот же текст, что
        // в оригинале брался из таблицы имён.
        const char *name = mpd_tag_name(tag);
        tagCombo_->addItem(QString::fromUtf8(name ? name : "?"), static_cast<int>(tag));
    }
    currentTag_ = wanted;
    tagCombo_->setCurrentIndex(static_cast<int>(tags.indexOf(wanted)));

    pendingValues_ = cmds_->tagValues(currentTag_);
    if (pendingValues_.isEmpty()) {
        scanHint_->setText(tr("No values"));
        return;
    }

    stepTagStats();
}

void ServerInfoDialog::stepTagStats()
{
    QElapsedTimer clock;
    clock.start();

    while (scanIndex_ < pendingValues_.size() && clock.elapsed() < kScanBudgetMs) {
        const auto row = cmds_->tagPlaytime(currentTag_, pendingValues_[scanIndex_]);
        ++scanIndex_;
        if (row)
            tagRows_ << *row;
    }

    scanHint_->setText(tr("Scanned %1 of %2…").arg(scanIndex_).arg(pendingValues_.size()));

    if (scanIndex_ < pendingValues_.size()) {
        // Остаток — следующим тактом событийного цикла: команда на значение дешёва,
        // но их тысячи, и в одном проходе окно замерло бы.
        QTimer::singleShot(0, this, &ServerInfoDialog::stepTagStats);
    } else {
        fillTable();
    }
}

void ServerInfoDialog::fillTable()
{
    sortTagRows(tagRows_);

    const quint64 maxPlay = tagRows_.isEmpty() ? 0 : tagRows_.first().playTimeSec;

    table_->setRowCount(static_cast<int>(tagRows_.size()));
    for (int i = 0; i < tagRows_.size(); ++i) {
        const TagPlaytime &row = tagRows_[i];
        auto *valueItem = new QTableWidgetItem(row.value);
        valueItem->setToolTip(tr("%1 tracks").arg(row.songs));
        table_->setItem(i, 0, valueItem);

        auto *timeItem = new QTableWidgetItem(formatSpan(row.playTimeSec));
        // Числовая длительность в роли — чтобы сортировка таблицы не разбирала подпись.
        timeItem->setData(Qt::UserRole, static_cast<qulonglong>(row.playTimeSec));
        table_->setItem(i, 1, timeItem);

        const int share = maxPlay == 0 ? 0
                                       : static_cast<int>((100 * row.playTimeSec) / maxPlay);
        auto *shareItem = new QTableWidgetItem(QStringLiteral("%1 %").arg(share));
        table_->setItem(i, 2, shareItem);
    }

    scanHint_->clear();
}