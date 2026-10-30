/**
 * @file test-network-drain.h
 * @brief Lets in-flight HTTP requests finish before Qt shuts down.
 *
 * The provider suites fire a real request and return immediately, so a reply
 * is still in flight when the case ends.  AsyncDownload's
 * QNetworkAccessManager is a parentless singleton, which means it is
 * destroyed after QCoreApplication; by then Qt's TLS support has been
 * unregistered and the manager's worker dies inside
 * QSslConfigurationPrivate::deepCopyDefaultConfiguration().
 *
 * Two things follow, and both are needed:
 *   1. The application object must be constructed *after* the network plugin
 *      registrations and destroyed *before* them — i.e. on the stack in an
 *      explicit main(), not at file scope.  That is why these suites use
 *      DOCTEST_CONFIG_IMPLEMENT rather than ..._WITH_MAIN.
 *   2. Pending replies must actually be drained while the application is
 *      still alive, otherwise an offline machine leaves them hanging.
 *
 * Call waitForIdle() at the end of a case and again just before main()
 * returns.
 */

#pragma once

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

/// Longest a single drain will wait before giving up on pending replies.
static constexpr int kDrainMs = 2000;

/**
 * Runs the event loop for up to @a ms, returning early once @a quietMs pass
 * with nothing left to do.  Qt exposes no pending-reply count, and
 * QCoreApplication::hasPendingEvents() is protected, so a quiet window is the
 * practical stand-in for "drained".
 *
 * @a quietMs must stay well under QEventLoop's internal 50 ms wake-up
 * interval: at or above it, postEventQueueExpired looks like real work and the
 * loop never reports itself idle.  A host with no route to the provider keeps
 * the socket pending until the OS times out, so each drain costs up to @a ms —
 * bounded, and only on such a host.
 */
inline void waitForIdle(int ms = kDrainMs, int quietMs = 10)
{
    QEventLoop loop;
    QTimer cap;
    cap.setSingleShot(true);
    QObject::connect(&cap, &QTimer::timeout, &loop, &QEventLoop::quit);
    cap.start(ms);

    while (cap.isActive()) {
        QTimer::singleShot(quietMs, &loop, &QEventLoop::quit);
        loop.exec();
        // Replies finishing during the window queue work behind the quit timer,
        // which is exactly what the next iteration should observe: exec()
        // returns at once when the queue is still busy, and only a genuinely
        // empty window lets the loop run the full quiet period.
        loop.processEvents(QEventLoop::AllEvents);
    }
}
