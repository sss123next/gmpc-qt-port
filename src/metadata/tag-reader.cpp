/**
 * @file tag-reader.cpp
 * @brief Extract embedded cover art and lyrics from local audio files via TagLib.
 */

#include "tag-reader.h"
#include "tools/log.h"

#include <QFileInfo>
#include <QFile>
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/tbytevector.h>
#include <taglib/tbytevectorstream.h>
#include <taglib/id3v2.h>

// ── File-type-specific headers ──────────────────────────────────────────────
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/unsynchronizedlyricsframe.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/vorbisfile.h>
#include <taglib/xiphcomment.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>
#include <taglib/mp4coverart.h>
#include <taglib/apetag.h>
#include <taglib/apeitem.h>
#include <taglib/wavpackfile.h>
#include <taglib/matroskafile.h>
#include <taglib/matroskaattachments.h>
#include <taglib/matroskaattachedfile.h>

// ══════════════════════════════════════════════════════════════════════════════
// Embedded ID3v2 tag scanning (for ID3v2-in-Matroska fallback)
// ══════════════════════════════════════════════════════════════════════════════

/// Scan the raw file for an ID3v2 header, return its data or empty ByteVector.
static TagLib::ByteVector scanEmbeddedID3v2(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};

    // ID3v2 header: "ID3" magic + version + flags + 4-byte syncsafe size
    constexpr qint64 kId3HeaderSize = 10;
    constexpr qint64 kChunkSize = 4096;
    const QByteArray kId3Magic("ID3", 3);

    qint64 offset = 0;
    while (offset < f.size()) {
        if (!f.seek(offset))
            break;
        const QByteArray chunk = f.read(kChunkSize);
        if (chunk.isEmpty())
            break;

        int idx = chunk.indexOf(kId3Magic);
        if (idx < 0) {
            offset += chunk.size();
            continue;
        }

        const qint64 pos = offset + idx;
        // Read the full ID3v2 header at this position.
        if (!f.seek(pos))
            break;
        const QByteArray header = f.read(kId3HeaderSize);
        if (header.size() < kId3HeaderSize)
            break;

        // Validate: check that the size field is syncsafe.
        // ID3v2 size: 4 bytes, each with MSB=0 (syncsafe).
        const auto *h = reinterpret_cast<const unsigned char *>(header.constData());
        if ((h[6] & 0x80) || (h[7] & 0x80) || (h[8] & 0x80) || (h[9] & 0x80)) {
            // False positive — not syncsafe.
            offset = pos + 3;  // skip "ID3" and keep scanning
            continue;
        }

        // Compute tag size (syncsafe integer).
        quint32 tagSize = (h[6] << 21) | (h[7] << 14) | (h[8] << 7) | h[9];
        if (tagSize == 0)
            break;

        // Total ID3v2 data including header.
        const qint64 totalSize = kId3HeaderSize + tagSize;
        if (pos + totalSize > f.size())
            break;

        // Read the full ID3v2 block.
        if (!f.seek(pos))
            break;
        const QByteArray id3Data = f.read(static_cast<qint64>(totalSize));
        if (id3Data.size() < totalSize)
            break;

        LOG_INFO("TagReader: found embedded ID3v2 at offset %1, size %2 bytes",
                 pos, totalSize);
        return TagLib::ByteVector(id3Data.constData(), id3Data.size());
    }

    return {};
}

/// Parse an APIC frame from an already-opened ID3v2::Tag.
static EmbeddedCover coverFromID3v2Tag(const TagLib::ID3v2::Tag *tag)
{
    if (!tag) return {};

    const auto &frames = tag->frameList("APIC");
    if (frames.isEmpty()) return {};

    auto *apic = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame *>(frames.front());
    if (!apic || apic->picture().isEmpty()) return {};

    EmbeddedCover c;
    c.data = QByteArray(apic->picture().data(), apic->picture().size());
    c.mime = QString::fromStdString(apic->mimeType().to8Bit(true));
    return c;
}

/// Extract USLT lyrics from an already-opened ID3v2::Tag.
static QString lyricsFromID3v2Tag(const TagLib::ID3v2::Tag *tag)
{
    if (!tag) return {};

    const auto &frames = tag->frameList("USLT");
    if (frames.isEmpty()) return {};

    auto *uslt = dynamic_cast<TagLib::ID3v2::UnsynchronizedLyricsFrame *>(frames.front());
    if (!uslt) return {};

    return QString::fromStdString(uslt->text().to8Bit(true));
}

// ══════════════════════════════════════════════════════════════════════════════
// Embedded cover art extraction
// ══════════════════════════════════════════════════════════════════════════════

/// Try to read cover from an MPEG file (MP3 with ID3v2 APIC frame).
static EmbeddedCover coverFromMpeg(const QString &path)
{
    TagLib::MPEG::File file(path.toUtf8().data());
    if (!file.isValid() || !file.ID3v2Tag())
        return {};

    const auto &frames = file.ID3v2Tag()->frameList("APIC");
    if (frames.isEmpty())
        return {};

    // Use the first APIC frame.
    auto *apic = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame *>(frames.front());
    if (!apic || apic->picture().isEmpty())
        return {};

    EmbeddedCover c;
    c.data = QByteArray(apic->picture().data(), apic->picture().size());
    c.mime = QString::fromStdString(apic->mimeType().to8Bit(true));
    LOG_INFO("TagReader: embedded cover from MPEG (%1 bytes, %2)",
             c.data.size(), c.mime);
    return c;
}

/// Try to read cover from a FLAC file (METADATA_BLOCK_PICTURE).
static EmbeddedCover coverFromFlac(const QString &path)
{
    TagLib::FLAC::File file(path.toUtf8().data());
    if (!file.isValid())
        return {};

    const auto pics = file.pictureList();
    for (const auto *pic : pics) {
        if (pic && !pic->data().isEmpty()) {
            EmbeddedCover c;
            c.data = QByteArray(pic->data().data(), pic->data().size());
            c.mime = QString::fromStdString(pic->mimeType().to8Bit(true));
            LOG_INFO("TagReader: embedded cover from FLAC (%1 bytes, %2)",
                     c.data.size(), c.mime);
            return c;
        }
    }
    return {};
}

/// Try to read cover from an Ogg Vorbis / Opus file (METADATA_BLOCK_PICTURE
/// as base64-encoded FLAC picture block).
static EmbeddedCover coverFromOggVorbis(const QString &path)
{
    TagLib::Ogg::Vorbis::File file(path.toUtf8().data());
    if (!file.isValid())
        return {};

    auto *xiph = dynamic_cast<TagLib::Ogg::XiphComment *>(file.tag());
    if (!xiph)
        return {};

    const auto fields = xiph->fieldListMap();
    if (!fields.contains("METADATA_BLOCK_PICTURE"))
        return {};

    const TagLib::StringList &vals = fields["METADATA_BLOCK_PICTURE"];
    if (vals.isEmpty())
        return {};

    // The value is a base64-encoded FLAC Picture block.
    const TagLib::ByteVector raw = TagLib::ByteVector::fromBase64(vals.front().data(TagLib::String::UTF8));
    if (raw.isEmpty())
        return {};

    TagLib::FLAC::Picture pic(raw);
    if (pic.data().isEmpty())
        return {};

    EmbeddedCover c;
    c.data = QByteArray(pic.data().data(), pic.data().size());
    c.mime = QString::fromStdString(pic.mimeType().to8Bit(true));
    LOG_INFO("TagReader: embedded cover from Ogg (%1 bytes, %2)",
             c.data.size(), c.mime);
    return c;
}

/// Try to read cover from an MP4/M4A file (covr atom).
static EmbeddedCover coverFromMp4(const QString &path)
{
    TagLib::MP4::File file(path.toUtf8().data());
    if (!file.isValid())
        return {};

    auto *tag = file.tag();
    if (!tag)
        return {};

    const auto &items = tag->itemMap();
    if (!items.contains("covr"))
        return {};

    const auto &coverList = items["covr"].toCoverArtList();
    if (coverList.isEmpty())
        return {};

    const auto &art = coverList.front();
    if (art.data().isEmpty())
        return {};

    EmbeddedCover c;
    c.data = QByteArray(art.data().data(), art.data().size());
    // MP4 covr format is implicitly JPEG or PNG; libmp4v2 stores format.
    c.mime = (art.format() == TagLib::MP4::CoverArt::PNG) ? "image/png" : "image/jpeg";
    LOG_INFO("TagReader: embedded cover from MP4 (%1 bytes, %2)",
             c.data.size(), c.mime);
    return c;
}

/// Try to read cover from an APE / WavPack file (COVER ART (FRONT) item).
static EmbeddedCover coverFromApe(const QString &path)
{
    TagLib::WavPack::File file(path.toUtf8().data());
    if (!file.isValid())
        return {};

    auto *ape = file.APETag();
    if (!ape)
        return {};

    const auto &items = ape->itemListMap();
    if (!items.contains("COVER ART (FRONT)"))
        return {};

    const TagLib::ByteVector raw = items["COVER ART (FRONT)"].binaryData();
    if (raw.isEmpty())
        return {};

    // APE item value is "filename\\0binarydata".
    const int sep = raw.find('\0');
    if (sep < 0 || sep >= static_cast<int>(raw.size()) - 1)
        return {};

    const TagLib::ByteVector imgData = raw.mid(sep + 1);
    if (imgData.isEmpty())
        return {};

    EmbeddedCover c;
    c.data = QByteArray(imgData.data(), imgData.size());
    LOG_INFO("TagReader: embedded cover from APE/WavPack (%1 bytes)", c.data.size());
    return c;
}

// ══════════════════════════════════════════════════════════════════════════════
// Public API
// ══════════════════════════════════════════════════════════════════════════════

/// Try to read cover from a Matroska file (MKV/MKA attachments + embedded ID3v2).
static EmbeddedCover coverFromMatroska(const QString &path)
{
    TagLib::Matroska::File file(path.toUtf8().data());
    if (!file.isValid())
        return {};

    // 1. Try native Matroska attachments (AttachedFile with image/ MIME).
    auto *attachments = file.attachments();
    if (attachments) {
        const auto &files = attachments->attachedFileList();
        for (const auto &att : files) {
            const QString mime = QString::fromStdString(
                att.mediaType().to8Bit(true));
            if (!mime.startsWith(QLatin1String("image/")))
                continue;
            const TagLib::ByteVector &raw = att.data();
            if (raw.isEmpty())
                continue;
            EmbeddedCover c;
            c.data = QByteArray(raw.data(), static_cast<int>(raw.size()));
            c.mime = mime;
            LOG_INFO("TagReader: embedded cover from Matroska attachment (%1 bytes, %2)",
                     c.data.size(), c.mime);
            return c;
        }
    }

    // 2. Fallback: scan for an embedded ID3v2 tag (older mkvmerge convention).
    const TagLib::ByteVector id3data = scanEmbeddedID3v2(path);
    if (!id3data.isEmpty()) {
        // Wrap raw ID3v2 bytes in a memory stream and parse via MPEG::File.
        // ID3v2Tag() returns the parsed tag even when isValid() is false.
        TagLib::ByteVectorStream stream(id3data);
        TagLib::MPEG::File mpegFile(&stream);
        if (mpegFile.ID3v2Tag()) {
            EmbeddedCover c = coverFromID3v2Tag(mpegFile.ID3v2Tag());
            if (!c.data.isEmpty()) {
                LOG_INFO("TagReader: embedded cover from Matroska ID3v2 (%1 bytes, %2)",
                         c.data.size(), c.mime);
                return c;
            }
        }
    }

    return {};
}

EmbeddedCover readEmbeddedCover(const QString &filePath)
{
    if (filePath.isEmpty() || !QFileInfo::exists(filePath))
        return {};

    const QString ext = QFileInfo(filePath).suffix().toLower();

    EmbeddedCover cover;

    if (ext == "mp3" || ext == "mp2")
        cover = coverFromMpeg(filePath);
    else if (ext == "flac")
        cover = coverFromFlac(filePath);
    else if (ext == "ogg" || ext == "opus")
        cover = coverFromOggVorbis(filePath);
    else if (ext == "m4a" || ext == "mp4" || ext == "aac" || ext == "alac")
        cover = coverFromMp4(filePath);
    else if (ext == "wv" || ext == "ape")
        cover = coverFromApe(filePath);
    else if (ext == "mkv" || ext == "mka")
        cover = coverFromMatroska(filePath);
    // WMA / ASF: not commonly embedded, skip for now.
    // AIFF, DSF, DFF: rarely carry embedded cover art.

    if (cover.data.isEmpty())
        LOG_INFO("TagReader: no embedded cover in %1", filePath);

    return cover;
}

// ══════════════════════════════════════════════════════════════════════════════
// Embedded lyrics extraction
// ══════════════════════════════════════════════════════════════════════════════

/// Extract lyrics from a generic TagLib::FileRef using property maps.
static QString lyricsFromProperties(TagLib::FileRef &ref)
{
    const auto props = ref.file()->properties();
    // Common tag names for lyrics across formats.
    for (const char *key : {"LYRICS", "UNSYNCEDLYRICS", "USLT"}) {
        if (props.contains(key)) {
            const auto &vals = props[key];
            if (!vals.isEmpty()) {
                const QString text = QString::fromStdString(
                    vals.front().to8Bit(true));
                if (!text.trimmed().isEmpty())
                    return text;
            }
        }
    }
    return {};
}

/// Extract lyrics from ID3v2 USLT frame specifically (richer structure).
static QString lyricsFromID3v2(const QString &path)
{
    TagLib::MPEG::File file(path.toUtf8().data());
    if (!file.isValid() || !file.ID3v2Tag())
        return {};

    const auto &frames = file.ID3v2Tag()->frameList("USLT");
    if (frames.isEmpty())
        return {};

    // Use the first USLT frame (typically the unsynchronized lyrics).
    auto *uslt = dynamic_cast<TagLib::ID3v2::UnsynchronizedLyricsFrame *>(frames.front());
    if (!uslt)
        return {};

    const QString text = QString::fromStdString(uslt->text().to8Bit(true));
    if (text.trimmed().isEmpty())
        return {};

    LOG_INFO("TagReader: extracted lyrics from ID3v2 USLT (%1 chars)", text.size());
    return text;
}

QString readEmbeddedLyrics(const QString &filePath)
{
    if (filePath.isEmpty() || !QFileInfo::exists(filePath))
        return {};

    // 1. Try format-specific extraction first.
    const QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext == "mp3" || ext == "mp2") {
        const QString uslt = lyricsFromID3v2(filePath);
        if (!uslt.isEmpty())
            return uslt;
    }

    // 2. Generic property-map approach for any format.
    TagLib::FileRef ref(filePath.toUtf8().data());
    if (!ref.isNull() && ref.file()) {
        const QString text = lyricsFromProperties(ref);
        if (!text.isEmpty()) {
            LOG_INFO("TagReader: extracted lyrics from property map (%1 chars)", text.size());
            return text;
        }
    }

    // 3. For Matroska files, also scan for an embedded ID3v2 tag (older
    //    convention) which may carry USLT frames.
    if (ext == "mkv" || ext == "mka") {
        const TagLib::ByteVector id3data = scanEmbeddedID3v2(filePath);
        if (!id3data.isEmpty()) {
            TagLib::ByteVectorStream stream(id3data);
            TagLib::MPEG::File mpegFile(&stream);
            if (mpegFile.ID3v2Tag()) {
                const QString text = lyricsFromID3v2Tag(mpegFile.ID3v2Tag());
                if (!text.isEmpty()) {
                    LOG_INFO("TagReader: extracted lyrics from Matroska ID3v2 (%1 chars)",
                             text.size());
                    return text;
                }
            }
        }
    }

    LOG_INFO("TagReader: no embedded lyrics in %1", filePath);
    return {};
}
