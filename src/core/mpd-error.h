#pragma once

/**
 * @file mpd-error.h
 * @brief Классификация ошибок libmpdclient: что делать с соединением дальше.
 *
 * Заголовки libmpdclient обещают, что MPD_ERROR_STATE восстановим, но на живом
 * MPD 2.24 это не так (проверено тестовой программой build/rf_probe.c): после
 * «Cannot send a new command while receiving another response» не помогает ни
 * mpd_response_finish(), ни mpd_connection_clear_error() — последний возвращает
 * true, а соединение остаётся нерабочим до переподключения. Поэтому STATE
 * отнесён к Reconnect.
 */

/// Действие соединения после неудачной команды libmpdclient.
enum class MpdErrorAction {
    None,             ///< ошибки нет (MPD_ERROR_SUCCESS)
    KeepConnection,   ///< команду отклонил сервер — соединение пригодно дальше
    Reconnect,        ///< соединением больше нельзя пользоваться
};

/** @brief Классифицирует ошибку по коду libmpdclient.
 *  @param code значение enum mpd_error из mpd_connection_get_error().
 *
 *  int вместо enum mpd_error, чтобы хедер не тянул <mpd/client.h> и классификацию
 *  можно было тестировать без библиотеки.
 */
[[nodiscard]] MpdErrorAction classifyMpdError(int code);
