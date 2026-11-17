#include "cache-sqlite.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>

CacheSqlite::CacheSqlite(const QString &path)
{
    // Ensure the parent directory exists — QStandardPaths doesn't create it.
    QDir().mkpath(QFileInfo(path).absolutePath());

    if (sqlite3_open(path.toUtf8().constData(), &db_) != SQLITE_OK) {
        qWarning() << "Failed to open metadata cache:" << path;
        db_ = nullptr;
        return;
    }
    // Limit page cache to ~512 KB and use in-memory journal to reduce heap.
    sqlite3_exec(db_, "PRAGMA cache_size=-128", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA journal_mode=MEMORY", nullptr, nullptr, nullptr);

    const char *sql = R"(
        CREATE TABLE IF NOT EXISTS cache (
            key   TEXT PRIMARY KEY,
            value BLOB,
            added INTEGER NOT NULL DEFAULT (strftime('%s','now'))
        )
    )";
    char *err = nullptr;
    sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (err) {
        qWarning() << "Cache init:" << err;
        sqlite3_free(err);
    }
}

CacheSqlite::~CacheSqlite()
{
    if (db_) sqlite3_close(db_);
}

bool CacheSqlite::put(const QString &key, const QByteArray &data)
{
    if (!db_) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "INSERT OR REPLACE INTO cache (key, value) VALUES (?, ?)", -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, key.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 2, data.constData(), data.size(), SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

QByteArray CacheSqlite::get(const QString &key) const
{
    if (!db_) return {};
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT value FROM cache WHERE key = ?", -1, &stmt, nullptr) != SQLITE_OK) return {};
    sqlite3_bind_text(stmt, 1, key.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    QByteArray result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        auto *blob = sqlite3_column_blob(stmt, 0);
        int sz = sqlite3_column_bytes(stmt, 0);
        result = QByteArray(static_cast<const char *>(blob), sz);
    }
    sqlite3_finalize(stmt);
    return result;
}

bool CacheSqlite::remove(const QString &key)
{
    if (!db_) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "DELETE FROM cache WHERE key = ?", -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, key.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

void CacheSqlite::cleanup()
{
    if (!db_) return;
    sqlite3_exec(db_, "DELETE FROM cache WHERE added < strftime('%s','now','-30 days')",
                 nullptr, nullptr, nullptr);
}
