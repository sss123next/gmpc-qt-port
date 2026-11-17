#pragma once

/**
 * @file metadata-map.h
 * @brief Текстовые поля MPRIS `Metadata`, собранные из тегов трэка.
 *
 * Внешние клиенты (`org.mpris.MediaPlayer2`) показывают пустоту, если в карте
 * нет artist/title: `status` отдаёт только идентификатор и длительность. Поэтому
 * поля собираются здесь — чистой функцией, без DBus и без соединения.
 */

#include <QString>
#include <QVariantMap>

/** @brief Карта метаданных трэка для MPRIS.
 *
 *  Пустые теги в карту не попадают: клиент различает «поля нет» и «поле пустое»,
 *  а подставлять пустоту вместо отсутствия значит врать. Ключи — `mpris:` + имя
 *  поля, как у уже существующего `mpris:trackid`.
 *
 *  @param title        тег Title
 *  @param artist       тег Artist
 *  @param album        тег Album
 *  @param totalSeconds длительность в секундах; ≤ 0 — неизвестна
 */
QVariantMap mprisTextMetadata(const QString &title, const QString &artist,
                              const QString &album, double totalSeconds);