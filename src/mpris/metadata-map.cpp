/**
 * @file metadata-map.cpp
 * @brief Сборка текстовых полей MPRIS `Metadata` из тегов трэка.
 */

#include "metadata-map.h"

namespace {

/// Пустой тег — это отсутствие поля, а не пустое значение: клиент различает их сам.
void insertIfNotEmpty(QVariantMap &m, const char *key, const QString &value)
{
    if (!value.isEmpty())
        m.insert(key, value);
}

} // namespace

QVariantMap mprisTextMetadata(const QString &title, const QString &artist,
                              const QString &album, double totalSeconds)
{
    QVariantMap m;

    // Длительность MPRIS ждёт в тех же единицах, что и `Position` (микросекунды),
    // поэтому перевод тот же, что был у метаданных до этого.
    if (totalSeconds > 0)
        m.insert("mpris:length", static_cast<qlonglong>(totalSeconds * 1000000));

    insertIfNotEmpty(m, "mpris:title", title);
    insertIfNotEmpty(m, "mpris:artist", artist);
    insertIfNotEmpty(m, "mpris:album", album);

    return m;
}