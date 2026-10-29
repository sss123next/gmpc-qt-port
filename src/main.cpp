/**
 * @file main.cpp
 * @brief Application entry point.
 *
 * Initialises QApplication, loads settings, and shows MainWindow.
 * Configuration is serialised via the Config singleton (QSettings).
 */

#include <QApplication>
#include "gui/mainwindow.h"
#include "core/config.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("gmpc-qt");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("gmpc-qt");

    MainWindow w;
    w.show();

    return app.exec();
}
