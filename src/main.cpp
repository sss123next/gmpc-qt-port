/**
 * @file main.cpp
 * @brief Application entry point.
 *
 * Initialises QApplication, loads settings, and shows MainWindow.
 * Configuration is serialised via the Config singleton (QSettings).
 */

#include <QApplication>
#include <QLoggingCategory>
#include "gui/mainwindow.h"
#include "core/config.h"
#include "core/mpd-env.h"

int main(int argc, char *argv[])
{
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

    MainWindow w;
    w.show();

    return app.exec();
}
