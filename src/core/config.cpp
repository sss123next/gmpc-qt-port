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

bool Config::startHidden() const
    { return s_.value("window/startHidden", false).toBool(); }
void Config::setStartHidden(bool v)
    { s_.setValue("window/startHidden", v); }

bool Config::stopOnExit() const
    { return s_.value("playback/stopOnExit", false).toBool(); }
void Config::setStopOnExit(bool v)
    { s_.setValue("playback/stopOnExit", v); }

bool Config::trayEnabled() const
    { return s_.value("tray/enabled", true).toBool(); }
void Config::setTrayEnabled(bool v)
    { s_.setValue("tray/enabled", v); }

void Config::sync()
    { s_.sync(); }
