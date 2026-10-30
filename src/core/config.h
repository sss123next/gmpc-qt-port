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
#include <QList>

/// One saved connection profile.
struct ConnectionProfile {
    QString name;
    QString host     = QStringLiteral("localhost");
    int     port     = 6600;
    QString password;
    QString socketPath;   ///< Unix socket; non-empty overrides host:port.
    bool    autoConnect = true;
};

/// Named multimedia-key actions (must match MediaKeyFilter actions).
enum class MediaKeyAction : int {
    None,
    Play,
    Pause,
    PlayPause,
    Stop,
    Next,
    Previous,
    Rewind,
    Forward,
    VolumeUp,
    VolumeDown,
    Mute,
};

/// One bindable multimedia key / action pair.
struct MediaKeyBinding {
    quint32 keysym = 0;          ///< X11 keysym (e.g. 0x1008ff14 for XF86AudioPrev)
    MediaKeyAction action = MediaKeyAction::None;
};

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
    int windowX() const;
    int windowY() const;
    bool windowMaximized() const;
    void setWindowPos(int x, int y);
    void setWindowMaximized(bool v);
    bool startHidden() const;
    void setStartHidden(bool v);

    // ── Playback ────────────────────────────────────────────────────
    bool stopOnExit() const;
    void setStopOnExit(bool v);

    // ── Proxy ─────────────────────────────────────────────────────────
    QString proxyHost() const;
    void setProxyHost(const QString &h);
    int proxyPort() const;
    void setProxyPort(int p);
    bool proxyEnabled() const;
    void setProxyEnabled(bool v);

    // ── Server ────────────────────────────────────────────────────────
    QString musicDirectory() const;
    void setMusicDirectory(const QString &d);
    int reconnectDelay() const;
    void setReconnectDelay(int ms);
    int connectionTimeout() const;
    void setConnectionTimeout(int ms);

    // ── Playlist ────────────────────────────────────────────────────
    /** @brief Header markup string (e.g. "$artist — $album"). */
    QString playlistHeaderMarkup() const;
    void setPlaylistHeaderMarkup(const QString &m);
    /** @brief Highlight colour for the current track (empty = default). */
    QString playlistHighlightColor() const;
    void setPlaylistHighlightColor(const QString &c);

    // ── Metadata ────────────────────────────────────────────────────
    QString discogsToken() const;
    void setDiscogsToken(const QString &t);
    /** @brief Font family for metadata display (empty = default). */
    QString metadataFont() const;
    void setMetadataFont(const QString &f);
    /** @brief Text colour for metadata (empty = theme default). */
    QString metadataColor() const;
    void setMetadataColor(const QString &c);

    // ── Tray ─────────────────────────────────────────────────────────
    bool trayEnabled() const;
    void setTrayEnabled(bool v);

    // ── Profiles ─────────────────────────────────────────────────────
    /** @brief All saved connection profiles (read from QSettings). */
    QList<ConnectionProfile> profiles() const;

    /** @brief Save all profiles (replaces the full list). */
    void setProfiles(const QList<ConnectionProfile> &list);

    /** @brief Name of the currently active profile (empty = manual). */
    QString activeProfile() const;
    void setActiveProfile(const QString &name);

    // ── Multimedia keys ────────────────────────────────────────────
    /// Persistent key → action bindings (defaults match XF86Audio keysyms).
    QList<MediaKeyBinding> mediaKeyBindings() const;
    void setMediaKeyBindings(const QList<MediaKeyBinding> &list);
    /// Convenience: look up action for a keysym, or None if unbound.
    MediaKeyAction actionForKey(quint32 keysym) const;

    /// Flush pending changes to disk.
    void sync();

private:
    Config();
    QSettings s_;
};

