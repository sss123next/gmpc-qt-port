#pragma once

/**
 * @file config.h
 * @brief Application settings (singleton).
 *
 * Reads / writes persistent settings via QSettings (INI-style).
 * Thread-safe after construction — use from the main thread only.
 */

#include <QSettings>
#include <QString>

/// Singleton holding all persistent user preferences.
class Config {
public:
    static Config &instance();

    // ── Connection ──────────────────────────────────────────────────
    QString host() const;
    void setHost(const QString &h);
    int port() const;
    void setPort(int p);
    QString password() const;
    void setPassword(const QString &p);
    bool autoConnect() const;
    void setAutoConnect(bool v);
    /// Unix-domain socket path (overrides host:port when non-empty).
    QString socketPath() const;
    void setSocketPath(const QString &p);

    // ── Window ──────────────────────────────────────────────────────
    int windowWidth() const;
    int windowHeight() const;
    void setWindowSize(int w, int h);
    bool startHidden() const;
    void setStartHidden(bool v);

    // ── Playback ────────────────────────────────────────────────────
    bool stopOnExit() const;
    void setStopOnExit(bool v);

    // ── Tray ─────────────────────────────────────────────────────────
    bool trayEnabled() const;
    void setTrayEnabled(bool v);

    /// Flush pending changes to disk.
    void sync();

private:
    Config();
    QSettings s_;
};

