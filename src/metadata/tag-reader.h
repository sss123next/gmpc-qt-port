#pragma once

/**
 * @file tag-reader.h
 * @brief Extract embedded metadata (cover art, lyrics) from audio files
 *        via TagLib, before falling through to network providers.
 */

#include <QByteArray>
#include <QString>

/// Simple result container for embedded tag data.
struct EmbeddedCover {
    QByteArray data;   ///< raw image bytes (JPEG/PNG/…)
    QString    mime;   ///< MIME type hint, may be empty
};

/// Try to extract embedded cover art from a local audio file.
/// @returns populated EmbeddedCover on success, nullopt-style empty on failure.
EmbeddedCover readEmbeddedCover(const QString &filePath);

/// Try to extract embedded lyrics (USLT / LYRICS tag) from a local audio file.
/// @returns lyrics text on success, empty string if none found.
QString readEmbeddedLyrics(const QString &filePath);
