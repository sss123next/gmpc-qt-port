/**
 * @file mpd-env.cpp
 * @brief Resolution of MPD_* environment variables.
 */

#include "mpd-env.h"

#include <QByteArray>
#include <cstdlib>

namespace {

/// Reads a variable, returning an empty string when unset or blank.
QString readEnv(const char *name)
{
    const QByteArray raw = qgetenv(name);
    const QString value = QString::fromLocal8Bit(raw).trimmed();
    return value;
}

/// True when the value denotes a filesystem or abstract Unix socket.
bool looksLikeSocket(const QString &host)
{
    return host.startsWith(QLatin1Char('/'))
        || host.startsWith(QLatin1Char('@'));
}

/// Parses MPD_TIMEOUT: plain integer seconds, or "<n>ms" for milliseconds.
int parseTimeoutMs(const QString &value)
{
    if (value.isEmpty())
        return 0;

    if (value.endsWith(QLatin1String("ms"), Qt::CaseInsensitive)) {
        bool ok = false;
        const int ms = value.left(value.size() - 2).trimmed().toInt(&ok);
        return (ok && ms > 0) ? ms : 0;
    }

    bool ok = false;
    const int secs = value.toInt(&ok);
    if (!ok || secs <= 0)
        return 0;
    return secs * 1000;
}

/// Splits an optional "password@" prefix off a MPD_HOST value.
void splitHostPassword(QString &host, QString &password)
{
    const int at = host.lastIndexOf(QLatin1Char('@'));
    if (at <= 0)
        return;

    // "password@host"; an abstract socket such as "@mpd-socket" has the
    // '@' at index 0 and is therefore not a password separator.
    const QString prefix = host.left(at);
    if (prefix.isEmpty())
        return;

    password = prefix;
    host = host.mid(at + 1);
}

} // namespace

MpdEnvSettings mpdEnvFromEnvironment()
{
    MpdEnvSettings s;

    s.host = readEnv("MPD_HOST");
    s.password = readEnv("MPD_PASSWORD");

    if (!s.host.isEmpty())
        splitHostPassword(s.host, s.password);

    if (!s.host.isEmpty()) {
        s.hostFromSocket = looksLikeSocket(s.host);
        if (!s.hostFromSocket) {
            const int colon = s.host.lastIndexOf(QLatin1Char(':'));
            const int bracket = s.host.lastIndexOf(QLatin1Char(']'));
            if (colon > bracket) {
                const QString tail = s.host.mid(colon + 1);
                bool ok = false;
                const int p = tail.toInt(&ok);
                if (ok && p > 0 && p <= 65535) {
                    // "[::1]:10000" keeps its port outside the brackets.
                    // A bare "fe80::1" also ends in digits, so only strip
                    // when that cannot be an address fragment: either the
                    // host is bracketed, or the remainder has no ':'.
                    const QString head = s.host.left(colon);
                    const bool bracketed = head.startsWith(QLatin1Char('['))
                                        && head.endsWith(QLatin1Char(']'));
                    if (bracketed || !head.contains(QLatin1Char(':'))) {
                        s.port = p;
                        s.host = head;
                    }
                }
            }
        }
    }

    if (s.port <= 0) {
        bool ok = false;
        const int p = readEnv("MPD_PORT").toInt(&ok);
        if (ok && p > 0 && p <= 65535)
            s.port = p;
    }

    s.timeoutMs = parseTimeoutMs(readEnv("MPD_TIMEOUT"));
    return s;
}

MpdResolvedSettings::MpdResolvedSettings(const QString &explicitHost,
                                         int explicitPort,
                                         const QString &explicitPassword,
                                         const QString &defaultHost,
                                         int defaultPort,
                                         int explicitTimeoutMs)
{
    const MpdEnvSettings env = mpdEnvFromEnvironment();

    const QString host = !explicitHost.trimmed().isEmpty()
        ? explicitHost.trimmed() : env.host;

    if (!host.isEmpty()) {
        host_ = host;
        unixSocket_ = looksLikeSocket(host);
    } else {
        host_ = defaultHost;
    }

    port_ = explicitPort > 0 ? explicitPort
          : (env.port > 0 ? env.port : defaultPort);

    // An explicit password always wins; MPD_HOST's embedded prefix has
    // already precedence over MPD_PASSWORD inside mpdEnvFromEnvironment().
    password_ = !explicitPassword.isEmpty() ? explicitPassword : env.password;

    timeoutMs_ = explicitTimeoutMs > 0 ? explicitTimeoutMs : env.timeoutMs;
    fromEnv_ = !env.host.isEmpty() || env.port > 0 || !env.password.isEmpty()
            || env.timeoutMs > 0;

    // The port is meaningless for a socket path — normalise so callers do
    // not have to remember the rule.
    if (unixSocket_)
        port_ = 0;
}

MpdResolvedSettings MpdResolvedSettings::fromConfig(const QString &savedHost,
                                                    int savedPort,
                                                    const QString &savedPassword,
                                                    const QString &savedSocket,
                                                    int savedTimeoutMs,
                                                    bool hostMayComeFromEnv)
{
    const MpdEnvSettings env = mpdEnvFromEnvironment();
    const int timeout = savedTimeoutMs > 0 ? savedTimeoutMs : env.timeoutMs;

    // When the caller's host field is authoritative (a saved profile, for
    // instance) it is passed through untouched and the environment only
    // supplies the timeout.
    if (!hostMayComeFromEnv) {
        const QString socket = savedSocket.trimmed();
        const bool isSocket = !socket.isEmpty();
        return MpdResolvedSettings(false,
                                   isSocket ? socket : savedHost.trimmed(),
                                   isSocket ? 0 : savedPort,
                                   savedPassword,
                                   timeout,
                                   isSocket);
    }

    MpdResolvedSettings r(savedHost, savedPort, savedPassword,
                          QString(), 0, savedTimeoutMs);

    // A saved socket path is an explicit caller value, so it outranks
    // MPD_HOST; an empty one leaves the environment in play.
    const QString socket = savedSocket.trimmed();
    if (!socket.isEmpty()) {
        r.host_ = socket;
        r.unixSocket_ = true;
        r.port_ = 0;
    }
    return r;
}

MpdResolvedSettings::MpdResolvedSettings(bool /*tag*/, const QString &host,
                                         int port, const QString &password,
                                         int timeoutMs, bool unixSocket)
    : host_(host)
    , password_(password)
    , port_(port)
    , timeoutMs_(timeoutMs)
    , unixSocket_(unixSocket)
{}
