#ifndef GMPCQT_CACHE_SQLITE_H
#define GMPCQT_CACHE_SQLITE_H

/**
 * @file cache-sqlite.h
 * @brief SQLite-backed key–value cache for metadata.
 *
 * Stores cover-art blobs, lyrics, and other fetched data.
 * Keys are arbitrary strings; values are byte arrays.
 */

#include <QString>
#include <QByteArray>
#include <sqlite3.h>

/// Persistent metadata cache backed by a local SQLite database.
class CacheSqlite {
public:
    /// Open (or create) the database at \p path.
    explicit CacheSqlite(const QString &path);
    ~CacheSqlite();

    /// Store \p data under \p key (upsert semantics).
    bool put(const QString &key, const QByteArray &data);
    /// Retrieve data for \p key, or empty QByteArray if missing.
    QByteArray get(const QString &key) const;
    /// Remove a single entry.
    bool remove(const QString &key);
    /// Purge expired / stale entries.
    void cleanup();

private:
    sqlite3 *db_ = nullptr;
};

#endif // GMPCQT_CACHE_SQLITE_H
