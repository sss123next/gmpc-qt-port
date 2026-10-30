#pragma once

/**
 * @file mpris-player.h
 * @brief MPRIS v2.2 DBus interface (org.mpris.MediaPlayer2).
 *
 * Registers on the session bus and exposes the standard MPRIS
 * root interface and Player interface.  Updates properties in
 * response to MPD status changes via Connection.
 *
 * Reference: https://specifications.freedesktop.org/mpris-spec/latest/
 */

#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QVariantMap>

#include "core/connection.h"

class MpdCommands;

/// MPRIS root interface (org.mpris.MediaPlayer2).
class MprisRoot : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString Identity READ identity CONSTANT)
    Q_PROPERTY(bool CanQuit READ canQuit CONSTANT)
    Q_PROPERTY(bool CanRaise READ canRaise CONSTANT)
    Q_PROPERTY(bool HasTrackList READ hasTrackList CONSTANT)
public:
    explicit MprisRoot(QObject *parent = nullptr);

    QString identity() const { return QStringLiteral("gmpc-qt"); }
    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }

public slots:
    void Quit();
    void Raise();
};

/// MPRIS player interface (org.mpris.MediaPlayer2.Player).
class MprisPlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")

    Q_PROPERTY(QString PlaybackStatus READ playbackStatus NOTIFY PropertiesChanged)
    Q_PROPERTY(QVariantMap Metadata READ metadata NOTIFY PropertiesChanged)
    Q_PROPERTY(double Volume READ volume WRITE setVolume NOTIFY PropertiesChanged)
    Q_PROPERTY(qlonglong Position READ position NOTIFY PropertiesChanged)
    Q_PROPERTY(double MinimumRate READ minimumRate CONSTANT)
    Q_PROPERTY(double MaximumRate READ maximumRate CONSTANT)
    Q_PROPERTY(bool CanGoNext READ canGoNext NOTIFY PropertiesChanged)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious NOTIFY PropertiesChanged)
    Q_PROPERTY(bool CanPlay READ canPlay NOTIFY PropertiesChanged)
    Q_PROPERTY(bool CanPause READ canPause NOTIFY PropertiesChanged)
    Q_PROPERTY(bool CanSeek READ canSeek CONSTANT)
    Q_PROPERTY(bool CanControl READ canControl CONSTANT)

public:
    explicit MprisPlayerAdaptor(Connection *conn, MpdCommands *cmds,
                                QObject *parent = nullptr);

    QString playbackStatus() const;
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double v);
    qlonglong position() const;
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const;
    bool canPause() const;
    bool canSeek() const { return false; }
    bool canControl() const { return true; }
    double minimumRate() const { return 1.0; }
    double maximumRate() const { return 1.0; }

public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offsetMicros);
    void SetPosition(const QDBusObjectPath &trackId, qlonglong positionMicros);
    void OpenUri(const QString &uri);

signals:
    void PropertiesChanged(const QString &interface,
                           const QVariantMap &changed,
                           const QStringList &invalidated);

private slots:
    void onStatusChanged(const Connection::Status &s);

private:
    void notifyProperty(const char *name);

    Connection *conn_;
    MpdCommands *cmds_;
    Connection::Status lastStatus_;
};

/// Top-level orchestrator: owns both adaptors and registers on the bus.
class MprisPlayer : public QObject {
    Q_OBJECT
public:
    static constexpr const char *kService = "org.mpris.MediaPlayer2.gmpc-qt";
    static constexpr const char *kPath    = "/org/mpris/MediaPlayer2";

    explicit MprisPlayer(Connection *conn, MpdCommands *cmds,
                         QObject *parent = nullptr);
    ~MprisPlayer() override;

    /// Returns true if registration succeeded.
    bool isRegistered() const { return registered_; }

private:
    bool registered_ = false;
    MprisRoot *root_;
    MprisPlayerAdaptor *player_;
};
