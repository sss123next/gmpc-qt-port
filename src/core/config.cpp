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
    { return s_.value("connection/host", "localhost").toString(); }
void Config::setHost(const QString &h)
    { s_.setValue("connection/host", h); }

int Config::port() const
    { return s_.value("connection/port", 6600).toInt(); }
void Config::setPort(int p)
    { s_.setValue("connection/port", p); }

QString Config::password() const
    { return s_.value("connection/password", "").toString(); }
void Config::setPassword(const QString &p)
    { s_.setValue("connection/password", p); }

QString Config::socketPath() const
    { return s_.value("connection/socket", "").toString(); }
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
    { return s_.value("server/connectionTimeout", 5000).toInt(); }
void Config::setConnectionTimeout(int ms)
    { s_.setValue("server/connectionTimeout", ms); }

// ── Tray ──────────────────────────────────────────────────────────────────
bool Config::trayEnabled() const
    { return s_.value("tray/enabled", true).toBool(); }
void Config::setTrayEnabled(bool v)
    { s_.setValue("tray/enabled", v); }

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

void Config::sync()
    { s_.sync(); }
