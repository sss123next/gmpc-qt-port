/**
 * @file mpris-player.cpp
 * @brief MPRIS v2.2 DBus interface implementation.
 */

#include "mpris-player.h"
#include "core/mpd-commands.h"

#include <QDBusConnection>
#include <QCoreApplication>
#include <QUrl>

// ── MprisRoot (non-adaptor, plain QObject on the bus) ───────────────────────

MprisRoot::MprisRoot(QObject *parent)
    : QObject(parent)
{
}

void MprisRoot::Quit()
{
    QCoreApplication::quit();
}

void MprisRoot::Raise()
{
    // Bring window to front — implemented via parent chain in MainWindow
}

// ── MprisPlayerAdaptor ──────────────────────────────────────────────────────

MprisPlayerAdaptor::MprisPlayerAdaptor(Connection *conn, MpdCommands *cmds,
                                       QObject *parent)
    : QDBusAbstractAdaptor(parent)
    , conn_(conn)
    , cmds_(cmds)
{
    qRegisterMetaType<Connection::Status>();
    connect(conn_, &Connection::statusChanged,
            this, &MprisPlayerAdaptor::onStatusChanged);
}

QString MprisPlayerAdaptor::playbackStatus() const
{
    switch (lastStatus_.state) {
    case MPD_STATE_PLAY:
        return QStringLiteral("Playing");
    case MPD_STATE_PAUSE:
        return QStringLiteral("Paused");
    default:
        break;
    }
    return QStringLiteral("Stopped");
}

QVariantMap MprisPlayerAdaptor::metadata() const
{
    QVariantMap m;
    // Basic metadata — real song data would need PlaylistModel lookup.
    // For now identify the track by MPD song ID.
    m.insert("mpris:trackid",
             QVariant::fromValue(QDBusObjectPath(
                 QStringLiteral("/org/gmpc-qt/song/%1")
                 .arg(lastStatus_.songId))));
    if (lastStatus_.totalDuration > 0)
        m.insert("mpris:length",
                 static_cast<qlonglong>(lastStatus_.totalDuration * 1000000));
    return m;
}

double MprisPlayerAdaptor::volume() const
{
    return lastStatus_.volume >= 0
        ? lastStatus_.volume / 100.0
        : 0.0;
}

void MprisPlayerAdaptor::setVolume(double v)
{
    int pct = qBound(0, static_cast<int>(v * 100), 100);
    cmds_->setVolume(pct);
}

qlonglong MprisPlayerAdaptor::position() const
{
    return static_cast<qlonglong>(lastStatus_.elapsed * 1000000);
}

bool MprisPlayerAdaptor::canGoNext() const
{
    // Assuming there's always a next track — libmpdclient will handle
    // this gracefully even at end of queue.
    return true;
}

bool MprisPlayerAdaptor::canGoPrevious() const
{
    return true;
}

bool MprisPlayerAdaptor::canPlay() const
{
    return true;
}

bool MprisPlayerAdaptor::canPause() const
{
    return true;
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void MprisPlayerAdaptor::Next()      { cmds_->next(); }
void MprisPlayerAdaptor::Previous()  { cmds_->previous(); }
void MprisPlayerAdaptor::Pause()     { cmds_->pause(); }
void MprisPlayerAdaptor::PlayPause() { cmds_->togglePause(); }
void MprisPlayerAdaptor::Stop()      { cmds_->stop(); }
void MprisPlayerAdaptor::Play()      { cmds_->play(); }

void MprisPlayerAdaptor::Seek(qlonglong offsetMicros)
{
    // libmpdclient: mpd_run_seek_current(conn_, seconds)
    int curPos = qMax(0, static_cast<int>(lastStatus_.elapsed));
    int newSec = curPos + static_cast<int>(offsetMicros / 1000000);
    if (newSec >= 0)
        cmds_->seek(newSec);
}

void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath &trackId,
                                      qlonglong positionMicros)
{
    Q_UNUSED(trackId);
    int sec = static_cast<int>(positionMicros / 1000000);
    cmds_->seek(sec);
}

void MprisPlayerAdaptor::OpenUri(const QString &uri)
{
    // Expects a file:// URI or a relative MPD path.
    QString path = QUrl(uri).toLocalFile();
    if (path.isEmpty())
        path = uri; // treat as MPD URI
    cmds_->clearQueue();
    cmds_->addUri(path);
    cmds_->playPos(0);
}

// ── Private ──────────────────────────────────────────────────────────────────

void MprisPlayerAdaptor::onStatusChanged(const Connection::Status &s)
{
    lastStatus_ = s;

    QVariantMap changed;
    changed.insert("PlaybackStatus", playbackStatus());
    changed.insert("Metadata", metadata());
    changed.insert("Volume", volume());
    changed.insert("Position", position());
    changed.insert("CanGoNext", canGoNext());
    changed.insert("CanGoPrevious", canGoPrevious());
    changed.insert("CanPlay", canPlay());
    changed.insert("CanPause", canPause());

    emit PropertiesChanged(
        QStringLiteral("org.mpris.MediaPlayer2.Player"),
        changed, {});
}

void MprisPlayerAdaptor::notifyProperty(const char *name)
{
    QVariantMap m;
    m.insert(name, property(name));
    emit PropertiesChanged(
        QStringLiteral("org.mpris.MediaPlayer2.Player"),
        m, {});
}

// ── MprisPlayer ─────────────────────────────────────────────────────────────

MprisPlayer::MprisPlayer(Connection *conn, MpdCommands *cmds,
                         QObject *parent)
    : QObject(parent)
{
    auto bus = QDBusConnection::sessionBus();

    if (!bus.isConnected()) {
        qWarning("MprisPlayer: no session bus — skipping");
        return;
    }

    // Root interface object (registered as non-adaptor since Qt6
    // QDBusAbstractAdaptor handles method dispatch via the adaptor model,
    // but the root interface is simpler without Q_PROPERTY for methods.
    // We register it as a plain QObject on the bus path.
    root_ = new MprisRoot(this);
    if (!bus.registerObject(kPath, root_, QDBusConnection::ExportAllSlots |
                            QDBusConnection::ExportAllProperties))
    {
        qWarning("MprisPlayer: failed to register root object on %s", kPath);
        return;
    }

    // Player adaptor
    player_ = new MprisPlayerAdaptor(conn, cmds, this);
    if (!bus.registerObject(kPath, player_, QDBusConnection::ExportAdaptors)) {
        qWarning("MprisPlayer: failed to register player adaptor on %s", kPath);
        bus.unregisterObject(kPath);
        return;
    }

    // Request ownership of the well-known name
    if (!bus.registerService(kService)) {
        qWarning("MprisPlayer: failed to acquire service %s", kService);
        bus.unregisterObject(kPath);
        return;
    }

    registered_ = true;
}

MprisPlayer::~MprisPlayer()
{
    if (registered_) {
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterObject(kPath);
        bus.unregisterService(kService);
    }
}
