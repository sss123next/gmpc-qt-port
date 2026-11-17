/**
 * @file database-browser.cpp
 * @brief MPD music database browser with tag-based QTreeView + tag selector.
 */

#include "database-browser.h"
#include "music-database-model.h"
#include "core/config.h"

#include <mpd/tag.h>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QTreeView>

/// Combo index → MPD tag type mapping.
static enum mpd_tag_type comboIndexToTag(int index)
{
    switch (index) {
    case 0:  return MPD_TAG_ARTIST;
    case 1:  return MPD_TAG_GENRE;
    case 2:  return MPD_TAG_DATE;
    case 3:  return MPD_TAG_COMPOSER;
    default: return MPD_TAG_ARTIST;
    }
}

DatabaseBrowser::DatabaseBrowser(Connection *conn, QWidget *parent)
    : BrowserWidget(parent)
    , conn_(conn)
    , model_(new MusicDatabaseModel(conn, this))
    , view_(new QTreeView(this))
    , tagCombo_(new QComboBox(this))
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    // Top bar: label + tag combo
    auto *topBar = new QHBoxLayout;
    topBar->addWidget(new QLabel(tr("Group by:"), this));
    tagCombo_->addItem(tr("Artist"));
    tagCombo_->addItem(tr("Genre"));
    tagCombo_->addItem(tr("Year"));
    tagCombo_->addItem(tr("Composer"));
    connect(tagCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DatabaseBrowser::onTagChanged);
    topBar->addWidget(tagCombo_, 1);
    lay->addLayout(topBar);

    view_->setModel(model_);
    view_->setHeaderHidden(false);
    // Двойной клик здесь означает «добавить в очередь», а не «развернуть»: вместе с
    // раскрытием второй клик по альбому вываливал бы в очередь сотню трэков вместо
    // того, чтобы просто показать их. Разворачивают стрелку и клавиши Right/Left.
    view_->setExpandsOnDoubleClick(false);
    view_->setSortingEnabled(true);
    // Сортировка по умолчанию — по первой колонке (Artist / Album), A–Z.
    view_->sortByColumn(0, Qt::AscendingOrder);
    lay->addWidget(view_, 1);
}

DatabaseBrowser::~DatabaseBrowser() = default;

QVector<BrowserIntent> DatabaseBrowser::selectedIntents() const
{
    QVector<BrowserIntent> intents;
    for (const QModelIndex &idx : uniqueSelectedRows(view_)) {
        // Разбор строки один и тот же, что у двойного клика: меню не имеет права
        // предлагать то, чего активация сделать не может.
        const MusicDbActivation act = model_->activationFor(idx);
        if (!act.uri.isEmpty())
            intents.append(BrowserIntent{BrowserIntent::Kind::Song, act.uri, {}});
        else if (!act.constraints.isEmpty())
            intents.append(BrowserIntent{BrowserIntent::Kind::TagGroup, {}, act.constraints});
    }
    return intents;
}

void DatabaseBrowser::onTagChanged(int index)
{
    // Выбор запоминается и без соединения: иначе после переподключения экран
    // заполнился бы артистами, хотя пользователь выбрал жанр.
    currentTag_ = comboIndexToTag(index);

    if (!conn_ || !conn_->isConnected())
        return;

    model_->loadByTag(currentTag_);
}

void DatabaseBrowser::onActivate()
{
    // Группы всегда по текущему способу группировки: loadArtists() здесь сбрасывал бы
    // дерево к артистам после каждого переключения на жанр.
    if (conn_->isConnected())
        model_->loadByTag(currentTag_);
}

void DatabaseBrowser::onConnectionChanged(bool connected)
{
    setEnabled(connected);
    if (connected)
        model_->loadByTag(currentTag_);
}

void DatabaseBrowser::onDeactivate()
{
    if (Config::instance().lowMemoryMode())
        model_->clear();
}
