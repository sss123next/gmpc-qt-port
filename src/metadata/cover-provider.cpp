#include "cover-provider.h"

CoverProvider::CoverProvider(QObject *parent)
    : QObject(parent)
    , cache_("/tmp/gmpc-qt-cover-cache.db") // TODO: proper XDG path
{}

void CoverProvider::fetchCover(const QString &artist, const QString &album)
{
    Q_UNUSED(artist);
    Q_UNUSED(album);
    // TODO: try cached → LastFM → Discogs
}
