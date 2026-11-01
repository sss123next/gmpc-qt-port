/**
 * @file mpd-error.cpp
 * @brief Таблица соответствия кодов libmpdclient действиям над соединением.
 */

#include "mpd-error.h"

#include <mpd/client.h>

MpdErrorAction classifyMpdError(int code)
{
    switch (static_cast<enum mpd_error>(code)) {
    case MPD_ERROR_SUCCESS:
        return MpdErrorAction::None;

    // Сервер отклонил команду (ACK) либо ей не хватило аргументов: вины
    // соединения нет, оно продолжает работать.
    case MPD_ERROR_SERVER:
    case MPD_ERROR_ARGUMENT:
        return MpdErrorAction::KeepConnection;

    // OOM/TIMEOUT/SYSTEM/RESOLVER/MALFORMED/CLOSED — из документации
    // mpd_connection_clear_error(); STATE — по замерам на живом MPD 2.24.
    case MPD_ERROR_STATE:
    case MPD_ERROR_OOM:
    case MPD_ERROR_TIMEOUT:
    case MPD_ERROR_SYSTEM:
    case MPD_ERROR_RESOLVER:
    case MPD_ERROR_MALFORMED:
    case MPD_ERROR_CLOSED:
        return MpdErrorAction::Reconnect;
    }

    // Новый код из будущей версии libmpdclient: безопаснее переподключиться.
    return MpdErrorAction::Reconnect;
}
