/**
 * @file search-browser.cpp
 * @brief MPD search browser — performs full-text search on the music database.
 */

#include "search-browser.h"
#include "core/connection.h"

#include <mpd/search.h>
#include <mpd/song.h>
#include <mpd/tag.h>

#include <QHeaderView>
#include <QLineEdit>
#include <QTreeView>
#include <QVBoxLayout>

// ── SearchResultModel ───────────────────────────────────────────────────────

SearchResultModel::SearchResultModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int SearchResultModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : items_.size();
}

int SearchResultModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant SearchResultModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= items_.size())
        return {};

    const auto &item = items_.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColArtist:   return item.artist;
        case ColTitle:    return item.title;
        case ColAlbum:    return item.album;
        case ColGenre:    return item.genre;
        case ColDuration: {
            int mins = item.duration / 60;
            int secs = item.duration % 60;
            return QStringLiteral("%1:%2")
                .arg(mins)
                .arg(secs, 2, 10, QLatin1Char('0'));
        }
        }
    }

    return {};
}

QVariant SearchResultModel::headerData(int section, Qt::Orientation orient,
                                       int role) const
{
    if (orient != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case ColArtist:   return tr("Artist");
    case ColTitle:    return tr("Title");
    case ColAlbum:    return tr("Album");
    case ColGenre:    return tr("Genre");
    case ColDuration: return tr("Length");
    }
    return {};
}

void SearchResultModel::setResults(const QVector<SearchResultItem> &items)
{
    beginResetModel();
    items_ = items;
    endResetModel();
}

const SearchResultItem *SearchResultModel::resultAt(int row) const
{
    if (row < 0 || row >= items_.size())
        return nullptr;
    return &items_.at(row);
}

// ── SearchBrowser ───────────────────────────────────────────────────────────

SearchBrowser::SearchBrowser(Connection *conn, QWidget *parent)
    : BrowserWidget(parent)
    , conn_(conn)
    , model_(new SearchResultModel(this))
    , view_(new QTreeView(this))
    , searchEdit_(new QLineEdit(this))
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    searchEdit_->setPlaceholderText(tr("Search songs\u2026"));
    connect(searchEdit_, &QLineEdit::returnPressed,
            this, &SearchBrowser::executeSearch);

    view_->setModel(model_);
    view_->setRootIsDecorated(false);
    view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view_->setSortingEnabled(true);
    view_->header()->setStretchLastSection(true);
    view_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

    lay->addWidget(searchEdit_);
    lay->addWidget(view_);

    setEnabled(false); // disabled until connected
}

void SearchBrowser::onConnectionChanged(bool connected)
{
    setEnabled(connected);
}

void SearchBrowser::executeSearch()
{
    const QString query = searchEdit_->text().trimmed();
    if (query.isEmpty())
        return;

    if (!conn_ || !conn_->isConnected())
        return;

    // Build a list of tag types to search against
    static constexpr std::array tags = {
        MPD_TAG_ARTIST,     MPD_TAG_ALBUM_ARTIST,
        MPD_TAG_TITLE,      MPD_TAG_ALBUM,
        MPD_TAG_GENRE,      MPD_TAG_COMPOSER,
        MPD_TAG_PERFORMER,
    };

    if (!mpd_search_db_songs(conn_->raw(), false))
        return;

    bool constraintsOk = true;
    for (auto t : tags) {
        if (!mpd_search_add_tag_constraint(
                conn_->raw(), MPD_OPERATOR_DEFAULT,
                t, query.toUtf8().constData())) {
            constraintsOk = false;
            break;
        }
    }

    if (!constraintsOk || !mpd_search_commit(conn_->raw())) {
        mpd_search_cancel(conn_->raw());
        return;
    }

    // Collect results
    QVector<SearchResultItem> results;
    while (auto *song = mpd_recv_song(conn_->raw())) {
        SearchResultItem item;
        item.uri    = QString::fromUtf8(mpd_song_get_uri(song));
        item.artist = QString::fromUtf8(
            mpd_song_get_tag(song, MPD_TAG_ARTIST, 0));
        item.title  = QString::fromUtf8(
            mpd_song_get_tag(song, MPD_TAG_TITLE, 0));
        item.album  = QString::fromUtf8(
            mpd_song_get_tag(song, MPD_TAG_ALBUM, 0));
        item.genre  = QString::fromUtf8(
            mpd_song_get_tag(song, MPD_TAG_GENRE, 0));
        item.duration = mpd_song_get_duration(song);
        results.append(item);

        mpd_song_free(song);
    }

    model_->setResults(results);
}
