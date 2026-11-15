/**
 * @file main.cpp
 * @brief Application entry point.
 *
 * Initialises QApplication, parses command-line arguments, checks for a
 * running instance via IPC (QLocalServer), and shows MainWindow.
 * Configuration is serialised via the Config singleton (QSettings).
 */

#include <QApplication>
#include <QLoggingCategory>
#include <QLocalServer>
#include "gui/mainwindow.h"
#include "core/config.h"
#include "core/mpd-env.h"
#include "tools/ipc.h"

int main(int argc, char *argv[])
{
    // Must be created early so that QCommandLineParser can use it.
    QApplication app(argc, argv);
    app.setApplicationName("gmpc-qt");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("gmpc-qt");

    // The MPD_* variables are consumed deep inside Connection/Config; say
    // once what they resolved to so a surprise target is traceable.
    const MpdEnvSettings env = mpdEnvFromEnvironment();
    if (!env.host.isEmpty() || env.port > 0 || env.timeoutMs > 0) {
        QString target = env.host.isEmpty() ? QStringLiteral("localhost")
                                            : env.host;
        if (!env.hostFromSocket)
            target += QLatin1Char(':')
                    + QString::number(env.port > 0 ? env.port : 6600);
        if (env.timeoutMs > 0)
            target += QStringLiteral(" (timeout %1ms)").arg(env.timeoutMs);
        qInfo().noquote() << "MPD target from environment:" << target;
    }

    // ── Parse command-line arguments ──────────────────────────────────
    IpcCommand ipcCmd = parseIpcCommandLine(argc, argv);

    // ── Single-instance check (unless allowMultiple is set) ───────────
    if (!Config::instance().allowMultiple()) {
        if (sendIpcCommand(ipcCmd)) {
            // Another instance accepted our command — we are done.
            return 0;
        }
        // No other instance is running.
        if (ipcCmd.kind == IpcCommandKind::Quit) {
            qInfo("IPC: no running instance to quit.");
            return 0;
        }
    } else {
        // Multiple instances allowed — IPC commands make no sense.
        if (ipcCmd.kind == IpcCommandKind::Quit) {
            qInfo("Multiple instances allowed, --quit ignored.");
            return 0;
        }
    }

    // ── We are the first instance; start the IPC server ───────────────
    IpcServer *ipcServer = new IpcServer(&app);

    // Remove any stale socket left by a crashed previous instance.
    QLocalServer::removeServer(ipcSocketPath());

    MainWindow w;
    // Wire IPC commands to window slots *before* show() so that any
    // command arriving during window construction is queued correctly.
    QObject::connect(ipcServer, &IpcServer::showRequested,
                     &w, &MainWindow::showWindow);
    QObject::connect(ipcServer, &IpcServer::hideRequested,
                     &w, &MainWindow::hideWindow);
    QObject::connect(ipcServer, &IpcServer::toggleRequested,
                     &w, &MainWindow::toggleVisibility);
    QObject::connect(ipcServer, &IpcServer::quitRequested,
                     &w, &MainWindow::handleIpcQuit);
    QObject::connect(ipcServer, &IpcServer::preferencesRequested,
                     &w, &MainWindow::showPreferences);
    QObject::connect(ipcServer, &IpcServer::showNotificationRequested,
                     &w, &MainWindow::handleIpcShowNotification);
    QObject::connect(ipcServer, &IpcServer::urlRequested,
                     &w, &MainWindow::handleIpcUrl);
    QObject::connect(ipcServer, &IpcServer::streamRequested,
                     &w, &MainWindow::handleIpcUrl);

    // Start listening AFTER connecting signals so no race is possible.
    if (!ipcServer->start()) {
        // Should not happen — we already verified no other instance runs.
        qWarning("IPC: failed to start server, continuing without IPC.");
    }

    w.show();

    return app.exec();
}
