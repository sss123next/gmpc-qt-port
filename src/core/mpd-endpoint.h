#pragma once

/**
 * @file mpd-endpoint.h
 * @brief Настройки дополнительного соединения с MPD и его открытие.
 *
 * libmpdclient не потокобезопасна: одно соединение обслуживает один поток, а
 * общее соединение Connection занято синхронными командами GUI-потока. Поэтому
 * поиску (6.5) и слушателю событий (6.7) нужен собственный handle — с одними и
 * теми же настройками сервера и одним и тем же порядком открытия.
 */

#include <QString>

struct mpd_connection;

/// Адрес сервера для дополнительного соединения.
struct MpdEndpoint {
    QString host;           ///< имя хоста, IP или путь к unix-сокету
    int port = 0;           ///< TCP-порт (игнорируется для unix-сокета)
    QString password;       ///< пусто — без авторизации
    int timeoutMs = 0;      ///< таймаут сокета, 0 — по умолчанию библиотеки

    /// Пустой host — не адрес: запрос с ним даже не стоит отправлять.
    [[nodiscard]] bool isValid() const { return !host.isEmpty(); }

    // Сравнение нужно на переподключении: при сменившихся настройках старое
    // соединение не переиспользуется.
    bool operator==(const MpdEndpoint &o) const
    {
        return host == o.host && port == o.port && password == o.password
            && timeoutMs == o.timeoutMs;
    }
    bool operator!=(const MpdEndpoint &o) const { return !(*this == o); }
};

/** @brief Открывает соединение и выполняет password, если он задан.
 *  @param endpoint адрес и учётные данные
 *  @param error    сюда попадёт текст неудачи (может быть nullptr)
 *  @return handle либо nullptr; при неудаче handle уже освобождён.
 */
[[nodiscard]] mpd_connection *openMpdConnection(const MpdEndpoint &endpoint,
                                                QString *error = nullptr);
