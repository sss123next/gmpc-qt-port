/**
 * @file database-browser.cpp
 * @brief MPD music database browser with tag-based QTreeView + tag selector.
 */

#include "database-browser.h"
#include "music-database-model.h"

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
    view_->setExpandsOnDoubleClick(true);
    lay->addWidget(view_, 1);
}

DatabaseBrowser::~DatabaseBrowser() = default;

void DatabaseBrowser::onTagChanged(int index)
{
    if (!conn_ || !conn_->isConnected())
        return;

    model_->loadByTag(comboIndexToTag(index));
}

void DatabaseBrowser::onActivate()
{
    if (conn_->isConnected())
        model_->loadArtists();
}

void DatabaseBrowser::onConnectionChanged(bool connected)
{
    setEnabled(connected);
    if (connected)
        model_->loadArtists();
}
