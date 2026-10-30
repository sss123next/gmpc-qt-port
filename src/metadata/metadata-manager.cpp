#include "metadata-manager.h"
#include "cover-provider.h"
#include "cache-sqlite.h"
#include "providers/lyrdb.h"
#include "providers/lyrics-ovh.h"

MetadataManager::MetadataManager(const QString &cachePath,
                                 const QString &lastFmApiKey,
                                 QObject *parent)
    : QObject(parent)
    , coverProvider_(new CoverProvider(lastFmApiKey, cachePath, this))
    , lyricsProvider_(new LrcLibProvider(this))
    , lyricsFallback_(new LyricsOvhProvider(this))
    , cache_(new CacheSqlite(cachePath))
{
    // Forward cover signals — stash artist/album is done in requestCover
    connect(coverProvider_, &CoverProvider::coverReady, this,
            [this](const QByteArray &data) {
                if (!pendingArtist_.isEmpty()) {
                    onCoverData(pendingArtist_, pendingAlbum_, data);
                    pendingArtist_.clear();
                    pendingAlbum_.clear();
                }
            });

    connect(coverProvider_, &CoverProvider::notFound, this,
            [this]() {
                if (!pendingArtist_.isEmpty()) {
                    emit coverNotFound(pendingArtist_, pendingAlbum_);
                    pendingArtist_.clear();
                    pendingAlbum_.clear();
                }
            });

    // Forward lyrics signals
    connect(lyricsProvider_, &LrcLibProvider::lyricsFound, this,
            [this](const QString &plain, const QString &synced) {
                QString text = synced.isEmpty() ? plain : synced;
                if (!pendingArtistLyrics_.isEmpty()) {
                    // Cache lyrics
                    cachePut(QString("lyrics:%1:%2")
                             .arg(pendingArtistLyrics_, pendingTitleLyrics_),
                             text.toUtf8());
                    emit lyricsReady(pendingArtistLyrics_, pendingTitleLyrics_, text);
                    pendingArtistLyrics_.clear();
                    pendingTitleLyrics_.clear();
                }
            });

    // When LrcLib fails, try lyrics.ovh as fallback
    connect(lyricsProvider_, &LrcLibProvider::notFound, this,
            [this]() {
                if (!pendingArtistLyrics_.isEmpty()) {
                    lyricsFallback_->fetchLyrics(pendingArtistLyrics_,
                                                  pendingTitleLyrics_);
                }
            });

    connect(lyricsProvider_, &LrcLibProvider::error, this,
            [this](const QString &msg) {
                emit error(msg);
            });

    // ── Lyrics.ovh fallback ─────────────────────────────────────────
    connect(lyricsFallback_, &LyricsOvhProvider::lyricsFound, this,
            [this](const QString &plain) {
                if (!pendingArtistLyrics_.isEmpty()) {
                    cachePut(QString("lyrics:%1:%2")
                             .arg(pendingArtistLyrics_, pendingTitleLyrics_),
                             plain.toUtf8());
                    emit lyricsReady(pendingArtistLyrics_, pendingTitleLyrics_, plain);
                    pendingArtistLyrics_.clear();
                    pendingTitleLyrics_.clear();
                }
            });

    connect(lyricsFallback_, &LyricsOvhProvider::notFound, this,
            [this]() {
                if (!pendingArtistLyrics_.isEmpty()) {
                    emit lyricsNotFound(pendingArtistLyrics_, pendingTitleLyrics_);
                    pendingArtistLyrics_.clear();
                    pendingTitleLyrics_.clear();
                }
            });

    connect(lyricsFallback_, &LyricsOvhProvider::error, this,
            [this](const QString &msg) {
                emit error(msg);
            });
}

MetadataManager::~MetadataManager() = default;

void MetadataManager::requestCover(const QString &artist, const QString &album)
{
    // Check cache first
    QString key = QString("cover:%1:%2").arg(artist, album);
    QByteArray cached = cache_->get(key);
    if (!cached.isEmpty()) {
        emit coverReady(artist, album, cached);
        return;
    }

    // Stash for signal forwarding
    pendingArtist_ = artist;
    pendingAlbum_  = album;

    // Forward to CoverProvider
    coverProvider_->fetchCover(artist, album);
}

void MetadataManager::requestLyrics(const QString &artist, const QString &title)
{
    QString key = QString("lyrics:%1:%2").arg(artist, title);
    QByteArray cached = cache_->get(key);
    if (!cached.isEmpty()) {
        emit lyricsReady(artist, title, QString::fromUtf8(cached));
        return;
    }

    // Stash for later forwarding
    pendingArtistLyrics_ = artist;
    pendingTitleLyrics_ = title;

    lyricsProvider_->fetchLyrics(artist, title);
}

void MetadataManager::onCoverData(const QString &artist, const QString &album,
                                  const QByteArray &data)
{
    cache_->put(QString("cover:%1:%2").arg(artist, album), data);
    emit coverReady(artist, album, data);
}

void MetadataManager::onCoverNotFound(const QString &artist, const QString &album)
{
    emit coverNotFound(artist, album);
}

// ── Helper (private) ────────────────────────────────────────────────────────

void MetadataManager::cachePut(const QString &key, const QByteArray &data)
{
    cache_->put(key, data);
}
