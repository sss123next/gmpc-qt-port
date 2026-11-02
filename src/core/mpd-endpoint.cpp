#include "mpd-endpoint.h"

#include <QByteArray>
#include <QObject>
#include <mpd/client.h>

mpd_connection *openMpdConnection(const MpdEndpoint &endpoint, QString *error)
{
    auto fail = [error](const QString &text) -> mpd_connection * {
        if (error)
            *error = text;
        return nullptr;
    };

    if (!endpoint.isValid())
        return fail(QObject::tr("MPD: адрес сервера не задан"));

    const QByteArray host = endpoint.host.toUtf8();
    mpd_connection *conn = mpd_connection_new(host.constData(),
                                              static_cast<unsigned>(endpoint.port),
                                              static_cast<unsigned>(endpoint.timeoutMs));
    if (!conn)
        return fail(QObject::tr("не хватило памяти под соединение с MPD"));

    if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
        // get_error_message() на соединении без ошибки утверждает ассертом.
        const QString text = QString::fromUtf8(mpd_connection_get_error_message(conn));
        mpd_connection_free(conn);
        return fail(text);
    }

    if (!endpoint.password.isEmpty()) {
        QByteArray password = endpoint.password.toUtf8();
        if (!mpd_run_password(conn, password.data())) {
            const QString text = QString::fromUtf8(
                mpd_connection_get_error_message(conn));
            mpd_connection_clear_error(conn);
            mpd_connection_free(conn);
            return fail(text);
        }
    }

    // Соединение, которое сидит в idle, молчит минуты и часами; keepalive в
    // libmpdclient выключен по умолчанию, поэтому TCP-разрыв посреди NAT мы бы
    // заметили только при следующей команде. Для unix-сокета вызов безвреден.
    if (!endpoint.host.startsWith(QLatin1Char('/')))
        mpd_connection_set_keepalive(conn, true);

    if (error)
        error->clear();
    return conn;
}
