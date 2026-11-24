/**
 * @file unison.cpp
 * @brief Unison community-synced lyrics provider implementation.
 *
 * Single GET request to /lyrics?song=...&artist=... returns best matching
 * lyrics in TTML, LRC, or plain format.  Responses include ETag for caching.
 * Conditional requests via If-None-Match avoid re-downloading unchanged data.
 */

#include "unison.h"

#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QNetworkRequest>
#include <QDebug>

// ── Constants ──────────────────────────────────────────────────────────────

static const char *kApiBase   = "https://unison.betterlyrics.org";
static const char *kUserAgent = "gmpc-qt/0.4 (https://github.com/ssss/gmpc-qt; sss@dark-alexandr.net)";

// ── Constructor ─────────────────────────────────────────────────────────────

UnisonProvider::UnisonProvider(QObject *parent)
    : QObject(parent), dl_(new AsyncDownload(this))
{
    dl_->setTimeout(5000, 10000);  // 5s connect, 10s overall for lyrics
}

// ── Public API ─────────────────────────────────────────────────────────────

void UnisonProvider::fetchLyrics(const QString &artist, const QString &track,
                                 const QString &album, int durationSec)
{
    searchedArtist_ = artist;
    searchedTrack_ = track;
    searchedAlbum_ = album;
    searchedDuration_ = durationSec;

    QUrl url(QStringLiteral("%1/lyrics").arg(kApiBase));
    QUrlQuery q;
    q.addQueryItem("song", track);
    q.addQueryItem("artist", artist);
    if (!album.isEmpty())
        q.addQueryItem("album", album);
    if (durationSec > 0)
        q.addQueryItem("duration", QString::number(durationSec));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", QByteArray(kUserAgent));

    // If we already have an ETag for this track, send conditional request.
    const QString key = etagKey(artist, track);
    const QString cachedEtag = etagCache_.value(key);
    if (!cachedEtag.isEmpty()) {
        req.setRawHeader("If-None-Match", cachedEtag.toUtf8());
        qDebug() << "Unison: conditional request (ETag:" << cachedEtag << ")";
    }

    dl_->getMeta(req, [this](bool ok, const QByteArray &data,
                              int httpStatus, const QByteArray &etag) {
        onMetaReply(ok, data, httpStatus, etag);
    });
}

// ── ETag cache key ────────────────────────────────────────────────────────

QString UnisonProvider::etagKey(const QString &artist, const QString &track)
{
    return artist + QLatin1Char('\0') + track;
}

// ── Metadata-aware reply handler ──────────────────────────────────────────

void UnisonProvider::onMetaReply(bool ok, const QByteArray &data,
                                 int httpStatus, const QByteArray &etag)
{
    const QString key = etagKey(searchedArtist_, searchedTrack_);

    // 304 Not Modified — data unchanged, already cached in MetadataManager.
    // Silently return without emitting notFound/error.
    if (ok && httpStatus == 304) {
        qDebug() << "Unison: 304 Not Modified for" << searchedArtist_
                 << searchedTrack_;
        return;
    }

    // Save ETag on successful data-bearing response.
    if (ok && !data.isEmpty() && !etag.isEmpty()) {
        etagCache_.insert(key, QString::fromUtf8(etag));
        qDebug() << "Unison: cached ETag" << etag << "for" << searchedArtist_
                 << searchedTrack_;
    }

    // If we got a successful but empty response (and not 304, handled above),
    // treat as not-found.
    if (ok && data.isEmpty()) {
        emit notFound();
        return;
    }

    onReply(ok, data, httpStatus, etag);
}

// ── Legacy reply handler (used by test seam & fallback) ────────────────────

void UnisonProvider::onReply(bool ok, const QByteArray &data,
                             int httpStatus, const QByteArray &etag)
{
    Q_UNUSED(httpStatus);
    Q_UNUSED(etag);

    if (!ok) { emit error(tr("Unison: network error")); return; }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) { emit error(tr("Unison: invalid JSON")); return; }

    QJsonObject root = doc.object();

    // Check success field
    if (!root.value("success").toBool(false)) {
        QString code = root.value("code").toString();
        if (code == "NOT_FOUND" || code == "LOOKUP_PENDING") {
            emit notFound();
        } else {
            emit error(root.value("error").toString());
        }
        return;
    }

    QJsonObject dataObj = root.value("data").toObject();
    QString lyricsText = dataObj.value("lyrics").toString();
    QString format     = dataObj.value("format").toString();  // ttml, lrc, plain
    QString syncType   = dataObj.value("syncType").toString(); // richsync, linesync, plain

    // Emit attribution for found lyrics (required by Unison terms).
    QJsonObject submitter = dataObj.value("submitter").toObject();
    QString submitterName = submitter.value("displayName").toString();
    QString submitterUrl  = submitter.value("avatarUrl").toString();
    QString unisonUrl;
    QJsonValue urlVal = dataObj.value("url");
    if (urlVal.isString())
        unisonUrl = urlVal.toString();
    emit attributionInfo(unisonUrl, submitterName, submitterUrl);

    if (lyricsText.isEmpty()) { emit notFound(); return; }

    // Extract plain text (strip timing if needed)
    QString plain;
    QString synced;

    if (format == "ttml") {
        plain = extractPlain(lyricsText, "ttml");
        synced = lyricsText;
    } else if (format == "lrc") {
        static QRegularExpression lre(R"(\[\d{2}:\d{2}\.\d{2}\]|\[\d{2}:\d{2}\])");
        plain = QString(lyricsText).remove(lre).trimmed();
        synced = lyricsText;
    } else {
        plain = lyricsText;
        synced = QString();
    }

    if (plain.isEmpty() && synced.isEmpty()) { emit notFound(); return; }

    emit lyricsFound(plain, synced);
}

// ── Plain text extraction ─────────────────────────────────────────────────

QString UnisonProvider::extractPlain(const QString &lyrics, const QString &format)
{
    if (format != "ttml")
        return lyrics;

    // Simple TTML → plain text
    QString text = lyrics;
    text.remove(QRegularExpression("<\\?xml[^>]*\\?>"));
    text.replace(QRegularExpression("<br\\s*/?>"), "\n");
    text.remove(QRegularExpression("<[^>]*>"));
    text.replace("&amp;", "&");
    text.replace("&lt;", "<");
    text.replace("&gt;", ">");
    text.replace("&quot;", "\"");
    text.replace("&apos;", "'");
    text = text.trimmed();
    text.replace(QRegularExpression("\\n{3,}"), "\n\n");
    return text;
}
