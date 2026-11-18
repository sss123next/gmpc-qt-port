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
#include <QStringList>
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
    // Raw saved values only: an empty host or a zero port means "unset" and
    // is resolved from MPD_HOST/MPD_PORT at connect time (see mpd-env.h),
    // never folded in here.
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
    bool startMinimized() const;
    void setStartMinimized(bool v);

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

    // ── General ────────────────────────────────────────────────────────
    /// Allow multiple GUI instances (skip IPC single-instance check).
    bool allowMultiple() const;
    void setAllowMultiple(bool v);
    /// Search-as-you-type (live filtering) in playlist and search browsers.
    bool searchAsType() const;
    void setSearchAsType(bool v);

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

    // ── Playlist columns ───────────────────────────────────────────
    /** @brief Видимость колонки очереди по её ключу (`PlaylistModel::columnKey`).
     *
     *  Оригинал хранил это у каждой таблицы под своим именем
     *  (`<treeview>-colshow/<колонка>`, egg/eggcolumnmodel.c); нам хватает
     *  одной очереди, поэтому ключ один. По умолчанию колонка видна.
     */
    bool playlistColumnVisible(const QString &key) const;
    void setPlaylistColumnVisible(const QString &key, bool visible);
    /** @brief Было ли значение колонки когда-либо записано в настройки.
     *         Позволяет отличить «явно включено» от «дефолт QSettings». */
    bool hasPlaylistColumnSetting(const QString &key) const;

    /** @brief Порядок колонок очереди (ключи). Пустой список — исходный порядок. */
    QStringList playlistColumnsOrder() const;
    void setPlaylistColumnsOrder(const QStringList &keys);

    // ── Metadata ────────────────────────────────────────────────────
    QString discogsToken() const;
    void setDiscogsToken(const QString &t);
    /** @brief Font family for metadata display (empty = default). */
    QString metadataFont() const;
    void setMetadataFont(const QString &f);
    /** @brief Text colour for metadata (empty = theme default). */
    QString metadataColor() const;
    void setMetadataColor(const QString &c);
    /** @brief Master switch: completely disable all metadata loading. */
    bool metadataEnabled() const;
    void setMetadataEnabled(bool v);
    /** @brief Allow local metadata (embedded tags, local cover files). */
    bool metadataLocal() const;
    void setMetadataLocal(bool v);
    /** @brief Allow online metadata services (MusicBrainz, Last.FM, Deezer, Discogs, lyrics). */
    bool metadataOnline() const;
    void setMetadataOnline(bool v);

    // ── Performance ──────────────────────────────────────────────────
    /** @brief Low Memory Mode: выгружать данные скрытых вкладок, не загружать
     *         очередь и БД при старте. По умолчанию выключен. */
    bool lowMemoryMode() const;
    void setLowMemoryMode(bool v);

    // ── Tray ─────────────────────────────────────────────────────────
    bool trayEnabled() const;
    void setTrayEnabled(bool v);
    /// Close the main window to tray instead of quitting (requires tray enabled).
    bool closeToTray() const;
    void setCloseToTray(bool v);
    /// Show a system notification balloon on track change.
    bool trayNotify() const;
    void setTrayNotify(bool v);

    // ── Tray Popup ─────────────────────────────────────────────────────
    bool trayPopupEnabled() const;
    void setTrayPopupEnabled(bool v);
    int trayPopupPosition() const;
    void setTrayPopupPosition(int pos);
    int trayPopupTimeout() const;
    void setTrayPopupTimeout(int sec);
    int trayPopupStyle() const;
    void setTrayPopupStyle(int s);
    bool trayPopupShowCover() const;
    void setTrayPopupShowCover(bool v);
    bool trayPopupShowButtons() const;
    void setTrayPopupShowButtons(bool v);
    int trayPopupXOffset() const;
    void setTrayPopupXOffset(int x);
    int trayPopupYOffset() const;
    void setTrayPopupYOffset(int y);

    // ── Mini‑Player (detached popup) ───────────────────────────────────
    /// Master enable/disable toggle (default on).
    bool miniPlayerEnabled() const;
    void setMiniPlayerEnabled(bool v);
    /** Position mode: 0=Center, 1=TopLeft, 2=TopRight, 3=BottomLeft, 4=BottomRight, 5=Custom */
    int miniPlayerPositionMode() const;
    void setMiniPlayerPositionMode(int mode);
    /// Screen index to show on (-1 = primary / follow mouse).
    int miniPlayerScreen() const;
    void setMiniPlayerScreen(int idx);
    /// Horizontal margin from screen edge (px).
    int miniPlayerMarginX() const;
    void setMiniPlayerMarginX(int px);
    /// Vertical margin from screen edge (px).
    int miniPlayerMarginY() const;
    void setMiniPlayerMarginY(int px);
    /// Saved absolute X (for Custom mode and drag).
    int miniPlayerX() const;
    void setMiniPlayerX(int x);
    /// Saved absolute Y (for Custom mode and drag).
    int miniPlayerY() const;
    void setMiniPlayerY(int y);
    /// Saved width (for geometry restore).
    int miniPlayerW() const;
    void setMiniPlayerW(int w);
    /// Saved height (for geometry restore).
    int miniPlayerH() const;
    void setMiniPlayerH(int h);
    /// Enable mouse-drag repositioning (default off).
    bool miniPlayerDraggable() const;
    void setMiniPlayerDraggable(bool v);
    /// Hide mini-player on left-click on non-button, non-cover area (default on).
    bool miniPlayerHideOnClick() const;
    void setMiniPlayerHideOnClick(bool v);

    // ── Profiles ─────────────────────────────────────────────────────
    /** @brief All saved connection profiles (read from QSettings). */
    QList<ConnectionProfile> profiles() const;

    /** @brief Save all profiles (replaces the full list). */
    void setProfiles(const QList<ConnectionProfile> &list);

    /** @brief Name of the currently active profile (empty = manual). */
    QString activeProfile() const;
    void setActiveProfile(const QString &name);

    // ── Preferences dialog ──────────────────────────────────────────
    int preferencesWidth() const;
    int preferencesHeight() const;
    void setPreferencesSize(int w, int h);
    int preferencesX() const;
    int preferencesY() const;
    void setPreferencesPos(int x, int y);

    // ── Multimedia keys ────────────────────────────────────────────
    /// Master switch: disable all global media-key hotkeys (default enabled).
    bool globalHotkeysEnabled() const;
    void setGlobalHotkeysEnabled(bool v);
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

