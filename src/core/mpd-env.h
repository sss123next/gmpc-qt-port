#pragma once

/**
 * @file mpd-env.h
 * @brief MPD connection settings taken from the environment.
 *
 * Follows the convention shared by mpc, ncmpcpp and mpDris2:
 *
 *   MPD_HOST      host, or a path starting with '/' (or '@' for an
 *                 abstract socket) which is treated as a Unix socket.
 *                 A "password@host" prefix is split out as the password.
 *   MPD_PORT      TCP port (ignored for a Unix socket).
 *   MPD_PASSWORD  password; MPD_HOST's own prefix wins if both are set.
 *   MPD_TIMEOUT   connect/read timeout, in seconds (or milliseconds when
 *                 suffixed with "ms").
 *
 * Precedence, highest first:
 *   1. An explicit value supplied by the caller (e.g. a saved profile or
 *      a value the user typed into Preferences).
 *   2. The environment variable.
 *   3. The built-in default (localhost:6600, no password).
 *
 * Empty and whitespace-only variables count as unset.
 */

#include <QString>

/// Environment-derived connection settings.
///
/// Every field defaults to "not specified", so a caller can pass one of
/// these straight into resolveHost() and friends without filling it first.
struct MpdEnvSettings {
    QString host;        ///< Host or socket path (password stripped).
    QString password;
    int     port = 0;    ///< TCP port, 0 = unset
    int     timeoutMs = 0;
    bool    hostFromSocket = false; ///< host is a Unix socket path
};

/// Reads the relevant variables.
MpdEnvSettings mpdEnvFromEnvironment();

/// Applies "explicit wins over env" and folds in caller defaults.
///
/// @param explicitHost  caller-supplied host/socket ("" = unspecified)
/// @param explicitPort  caller-supplied TCP port (<= 0 = unspecified)
/// @param explicitPassword caller-supplied password ("" = unspecified)
/// @param defaultHost   fallback when neither caller nor env supplied one
/// @param defaultPort   fallback port when both are unspecified
class MpdResolvedSettings {
public:
    MpdResolvedSettings(const QString &explicitHost,
                        int explicitPort,
                        const QString &explicitPassword,
                        const QString &defaultHost = QString(),
                        int defaultPort = 0,
                        int explicitTimeoutMs = 0);

    /// Variant for the saved-settings shape, where a Unix socket lives in
    /// its own field instead of inside the host string.
    ///
    /// Every argument is a raw saved value: empty / zero means "unset" and
    /// is then filled from the environment.
    ///
    /// @param savedTimeoutMs  Config::connectionTimeout(); 0 = unset.
    /// @param hostMayComeFromEnv  false for a saved profile, whose host and
    ///        port are authoritative and must never be overridden by the
    ///        environment; only the timeout is then consulted.
    static MpdResolvedSettings fromConfig(const QString &savedHost,
                                          int savedPort,
                                          const QString &savedPassword,
                                          const QString &savedSocket,
                                          int savedTimeoutMs = 0,
                                          bool hostMayComeFromEnv = true);

    /// Host to hand to mpd_connection_new(); empty means unspecified, which
    /// libmpdclient resolves from the environment itself, else localhost.
    QString host() const { return host_; }
    /// TCP port (0 = library default / Unix socket).
    int port() const { return port_; }
    /// Password ("" = none).
    QString password() const { return password_; }
    /// Timeout in milliseconds (0 = library default).
    int timeoutMs() const { return timeoutMs_; }
    /// True when host() is a Unix-domain socket path.
    bool isUnixSocket() const { return unixSocket_; }
    /// True when host()/port()/password() came from the environment.
    bool fromEnvironment() const { return fromEnv_; }

private:
    MpdResolvedSettings(bool /*tag*/, const QString &host, int port,
                        const QString &password, int timeoutMs,
                        bool unixSocket);

    QString host_;
    QString password_;
    int port_ = 0;
    int timeoutMs_ = 0;
    bool unixSocket_ = false;
    bool fromEnv_ = false;
};
