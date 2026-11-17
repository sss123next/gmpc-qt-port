#include "config.h"

Config &Config::instance()
{
    static Config c;
    return c;
}

Config::Config()
    : s_("gmpc-qt", "gmpc-qt")
{}

QString Config::host() const
    { return s_.value("connection/host").toString().trimmed(); }
void Config::setHost(const QString &h)
    { s_.setValue("connection/host", h); }

int Config::port() const
    { return s_.value("connection/port").toInt(); }
void Config::setPort(int p)
    { s_.setValue("connection/port", p); }

QString Config::password() const
    { return s_.value("connection/password").toString(); }
void Config::setPassword(const QString &p)
    { s_.setValue("connection/password", p); }

QString Config::socketPath() const
    { return s_.value("connection/socket").toString().trimmed(); }
void Config::setSocketPath(const QString &p)
    { s_.setValue("connection/socket", p); }

bool Config::autoConnect() const
    { return s_.value("connection/autoconnect", true).toBool(); }
void Config::setAutoConnect(bool v)
    { s_.setValue("connection/autoconnect", v); }

int Config::windowWidth() const
    { return s_.value("window/width", 800).toInt(); }
int Config::windowHeight() const
    { return s_.value("window/height", 600).toInt(); }
void Config::setWindowSize(int w, int h)
    { s_.setValue("window/width", w); s_.setValue("window/height", h); }

int Config::windowX() const
    { return s_.value("window/x", -1).toInt(); }
int Config::windowY() const
    { return s_.value("window/y", -1).toInt(); }
bool Config::windowMaximized() const
    { return s_.value("window/maximized", false).toBool(); }
void Config::setWindowPos(int x, int y)
    { s_.setValue("window/x", x); s_.setValue("window/y", y); }
void Config::setWindowMaximized(bool v)
    { s_.setValue("window/maximized", v); }

bool Config::startHidden() const
    { return s_.value("window/startHidden", false).toBool(); }
void Config::setStartHidden(bool v)
    { s_.setValue("window/startHidden", v); }

bool Config::stopOnExit() const
    { return s_.value("playback/stopOnExit", false).toBool(); }
void Config::setStopOnExit(bool v)
    { s_.setValue("playback/stopOnExit", v); }

// ── Proxy ──────────────────────────────────────────────────────────────────
bool Config::proxyEnabled() const
    { return s_.value("proxy/enabled", false).toBool(); }
void Config::setProxyEnabled(bool v)
    { s_.setValue("proxy/enabled", v); }
QString Config::proxyHost() const
    { return s_.value("proxy/host", "").toString(); }
void Config::setProxyHost(const QString &h)
    { s_.setValue("proxy/host", h); }
int Config::proxyPort() const
    { return s_.value("proxy/port", 8080).toInt(); }
void Config::setProxyPort(int p)
    { s_.setValue("proxy/port", p); }

// ── General ───────────────────────────────────────────────────────────────
bool Config::allowMultiple() const
    { return s_.value("general/allowMultiple", false).toBool(); }
void Config::setAllowMultiple(bool v)
    { s_.setValue("general/allowMultiple", v); }
bool Config::searchAsType() const
    { return s_.value("general/searchAsType", false).toBool(); }
void Config::setSearchAsType(bool v)
    { s_.setValue("general/searchAsType", v); }

// ── Server ─────────────────────────────────────────────────────────────────
QString Config::musicDirectory() const
    { return s_.value("server/musicDirectory", "").toString(); }
void Config::setMusicDirectory(const QString &d)
    { s_.setValue("server/musicDirectory", d); }
int Config::reconnectDelay() const
    { return s_.value("server/reconnectDelay", 2000).toInt(); }
void Config::setReconnectDelay(int ms)
    { s_.setValue("server/reconnectDelay", ms); }
int Config::connectionTimeout() const
    { return s_.value("server/connectionTimeout").toInt(); }
void Config::setConnectionTimeout(int ms)
    { s_.setValue("server/connectionTimeout", ms); }

// ── Playlist ──────────────────────────────────────────────────────────────
QString Config::playlistHeaderMarkup() const
    { return s_.value("playlist/headerMarkup", "").toString(); }
void Config::setPlaylistHeaderMarkup(const QString &m)
    { s_.setValue("playlist/headerMarkup", m); }
QString Config::playlistHighlightColor() const
    { return s_.value("playlist/highlightColor", "").toString(); }
void Config::setPlaylistHighlightColor(const QString &c)
    { s_.setValue("playlist/highlightColor", c); }

bool Config::playlistColumnVisible(const QString &key) const
    { return s_.value("playlist/columns/" + key, true).toBool(); }
void Config::setPlaylistColumnVisible(const QString &key, bool visible)
    { s_.setValue("playlist/columns/" + key, visible); }

QStringList Config::playlistColumnsOrder() const
{
    // Пустая строка и «нет ключа» — одно: исходный порядок колонок модели.
    const QString raw = s_.value("playlist/columnOrder", "").toString();
    return raw.isEmpty() ? QStringList{} : raw.split(QLatin1Char(','), Qt::SkipEmptyParts);
}
void Config::setPlaylistColumnsOrder(const QStringList &keys)
    { s_.setValue("playlist/columnOrder", keys.join(QLatin1Char(','))); }

// ── Metadata ─────────────────────────────────────────────────────────────
QString Config::discogsToken() const
    { return s_.value("metadata/discogsToken", "").toString(); }
void Config::setDiscogsToken(const QString &t)
    { s_.setValue("metadata/discogsToken", t); }
QString Config::metadataFont() const
    { return s_.value("metadata/font", "").toString(); }
void Config::setMetadataFont(const QString &f)
    { s_.setValue("metadata/font", f); }
QString Config::metadataColor() const
    { return s_.value("metadata/color", "").toString(); }
void Config::setMetadataColor(const QString &c)
    { s_.setValue("metadata/color", c); }

// ── Tray ──────────────────────────────────────────────────────────────────
bool Config::trayEnabled() const
    { return s_.value("tray/enabled", true).toBool(); }
void Config::setTrayEnabled(bool v)
    { s_.setValue("tray/enabled", v); }
bool Config::closeToTray() const
    { return s_.value("tray/closeToTray", false).toBool(); }
void Config::setCloseToTray(bool v)
    { s_.setValue("tray/closeToTray", v); }
bool Config::trayNotify() const
    { return s_.value("tray/notify", false).toBool(); }
void Config::setTrayNotify(bool v)
    { s_.setValue("tray/notify", v); }

// ── Tray Popup ────────────────────────────────────────────────────────────
bool Config::trayPopupEnabled() const
    { return s_.value("tray/popupEnabled", true).toBool(); }
void Config::setTrayPopupEnabled(bool v)
    { s_.setValue("tray/popupEnabled", v); }
int Config::trayPopupPosition() const
    { return s_.value("tray/popupPosition", 0).toInt(); }
void Config::setTrayPopupPosition(int pos)
    { s_.setValue("tray/popupPosition", pos); }
int Config::trayPopupTimeout() const
    { return s_.value("tray/popupTimeout", 10).toInt(); }
void Config::setTrayPopupTimeout(int sec)
    { s_.setValue("tray/popupTimeout", sec); }
int Config::trayPopupStyle() const
    { return s_.value("tray/popupStyle", 0).toInt(); }
void Config::setTrayPopupStyle(int s)
    { s_.setValue("tray/popupStyle", s); }
bool Config::trayPopupShowCover() const
    { return s_.value("tray/popupShowCover", true).toBool(); }
void Config::setTrayPopupShowCover(bool v)
    { s_.setValue("tray/popupShowCover", v); }
bool Config::trayPopupShowButtons() const
    { return s_.value("tray/popupShowButtons", true).toBool(); }
void Config::setTrayPopupShowButtons(bool v)
    { s_.setValue("tray/popupShowButtons", v); }
int Config::trayPopupXOffset() const
    { return s_.value("tray/popupXOffset", 0).toInt(); }
void Config::setTrayPopupXOffset(int x)
    { s_.setValue("tray/popupXOffset", x); }
int Config::trayPopupYOffset() const
    { return s_.value("tray/popupYOffset", 0).toInt(); }
void Config::setTrayPopupYOffset(int y)
    { s_.setValue("tray/popupYOffset", y); }

// ── Mini‑Player ────────────────────────────────────────────────────────────
int Config::miniPlayerPositionMode() const
    { return s_.value("miniPlayer/positionMode", 0).toInt(); }
void Config::setMiniPlayerPositionMode(int mode)
    { s_.setValue("miniPlayer/positionMode", mode); }
int Config::miniPlayerScreen() const
    { return s_.value("miniPlayer/screen", -1).toInt(); }
void Config::setMiniPlayerScreen(int idx)
    { s_.setValue("miniPlayer/screen", idx); }
int Config::miniPlayerMarginX() const
    { return s_.value("miniPlayer/marginX", 10).toInt(); }
void Config::setMiniPlayerMarginX(int px)
    { s_.setValue("miniPlayer/marginX", px); }
int Config::miniPlayerMarginY() const
    { return s_.value("miniPlayer/marginY", 10).toInt(); }
void Config::setMiniPlayerMarginY(int px)
    { s_.setValue("miniPlayer/marginY", px); }
int Config::miniPlayerX() const
    { return s_.value("miniPlayer/x", -1).toInt(); }
void Config::setMiniPlayerX(int x)
    { s_.setValue("miniPlayer/x", x); }
int Config::miniPlayerY() const
    { return s_.value("miniPlayer/y", -1).toInt(); }
void Config::setMiniPlayerY(int y)
    { s_.setValue("miniPlayer/y", y); }
int Config::miniPlayerW() const
    { return s_.value("miniPlayer/w", -1).toInt(); }
void Config::setMiniPlayerW(int w)
    { s_.setValue("miniPlayer/w", w); }
int Config::miniPlayerH() const
    { return s_.value("miniPlayer/h", -1).toInt(); }
void Config::setMiniPlayerH(int h)
    { s_.setValue("miniPlayer/h", h); }

// ── Profiles ──────────────────────────────────────────────────────────────
QList<ConnectionProfile> Config::profiles() const
{
    QList<ConnectionProfile> list;
    const int count = s_.value("profiles/count", 0).toInt();
    for (int i = 0; i < count; ++i) {
        const QString prefix = QStringLiteral("profiles/%1/").arg(i);
        ConnectionProfile p;
        p.name        = s_.value(prefix + "name").toString();
        p.host        = s_.value(prefix + "host", "localhost").toString();
        p.port        = s_.value(prefix + "port", 6600).toInt();
        p.password    = s_.value(prefix + "password", "").toString();
        p.socketPath  = s_.value(prefix + "socket", "").toString();
        p.autoConnect = s_.value(prefix + "autoConnect", true).toBool();
        list.append(p);
    }
    return list;
}

void Config::setProfiles(const QList<ConnectionProfile> &list)
{
    // Remove old keys
    const int oldCount = s_.value("profiles/count", 0).toInt();
    for (int i = 0; i < oldCount; ++i)
        s_.remove(QStringLiteral("profiles/%1").arg(i));

    s_.setValue("profiles/count", list.size());
    for (int i = 0; i < list.size(); ++i) {
        const auto &p = list[i];
        const QString prefix = QStringLiteral("profiles/%1/").arg(i);
        s_.setValue(prefix + "name",        p.name);
        s_.setValue(prefix + "host",        p.host);
        s_.setValue(prefix + "port",        p.port);
        s_.setValue(prefix + "password",    p.password);
        s_.setValue(prefix + "socket",      p.socketPath);
        s_.setValue(prefix + "autoConnect", p.autoConnect);
    }
}

QString Config::activeProfile() const
    { return s_.value("profiles/active", "").toString(); }
void Config::setActiveProfile(const QString &name)
    { s_.setValue("profiles/active", name); }

// ── Default media key table ─────────────────────────────────────────────
static QList<MediaKeyBinding> defaultMediaKeyBindings()
{
    return {
        { 0x1008ff14, MediaKeyAction::Previous },  // XF86AudioPrev
        { 0x1008ff16, MediaKeyAction::Rewind   },  // XF86AudioRewind
        { 0x1008ff17, MediaKeyAction::Next     },  // XF86AudioNext
        { 0x1008ff19, MediaKeyAction::Forward  },  // XF86AudioForward
        { 0x1008ff13, MediaKeyAction::Stop     },  // XF86AudioStop
        { 0x1008ff15, MediaKeyAction::Pause    },  // XF86AudioPause
        { 0x1008ff11, MediaKeyAction::Play     },  // XF86AudioPlay
        { 0x1008ff12, MediaKeyAction::PlayPause},  // XF86AudioPlayPause
    };
}

static const char *mkGroup = "mediakeys";

QList<MediaKeyBinding> Config::mediaKeyBindings() const
{
    // If no saved bindings, return defaults
    if (!s_.childGroups().contains(QLatin1StringView(mkGroup)))
        return defaultMediaKeyBindings();

    QList<MediaKeyBinding> list;
    const int count = s_.value(QStringLiteral("%1/count").arg(mkGroup), 0).toInt();
    for (int i = 0; i < count; ++i) {
        const QString p = QStringLiteral("%1/%2/").arg(mkGroup).arg(i);
        MediaKeyBinding b;
        b.keysym = static_cast<quint32>(s_.value(p + "keysym", 0).toUInt());
        b.action = static_cast<MediaKeyAction>(
            s_.value(p + "action", 0).toInt());
        list.append(b);
    }
    return list;
}

void Config::setMediaKeyBindings(const QList<MediaKeyBinding> &list)
{
    // Remove old
    s_.remove(mkGroup);
    s_.setValue(QStringLiteral("%1/count").arg(mkGroup), list.size());
    for (int i = 0; i < list.size(); ++i) {
        const auto &b = list[i];
        const QString p = QStringLiteral("%1/%2/").arg(mkGroup).arg(i);
        s_.setValue(p + "keysym", static_cast<quint32>(b.keysym));
        s_.setValue(p + "action",  static_cast<int>(b.action));
    }
}

MediaKeyAction Config::actionForKey(quint32 keysym) const
{
    const auto bindings = mediaKeyBindings();
    for (const auto &b : bindings) {
        if (b.keysym == keysym)
            return b.action;
    }
    return MediaKeyAction::None;
}

void Config::sync()
    { s_.sync(); }
