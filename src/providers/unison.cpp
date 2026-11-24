/**
 * @file unison.cpp
 * @brief Unison community-synced lyrics provider implementation.
 *
 * Single GET request to /lyrics?song=...&artist=... returns best matching
 * lyrics in TTML, LRC, or plain format.  Responses include ETag for caching.
 */

#include "unison.h"

#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QDebug>

// ── Constants ──────────────────────────────────────────────────────────────

static const char *kApiBase   = "https://unison.betterlyrics.org";
static const char *kUserAgent = "gmpc-qt/0.3 (https://github.com/ssss/gmpc-qt; sss@dark-alexandr.net)";

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

    dl_->get(url, [this](bool ok, const QByteArray &data) {
        onReply(ok, data);
    });
}

// ── Response handler ───────────────────────────────────────────────────────

void UnisonProvider::onReply(bool ok, const QByteArray &data)
{
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
    // QString confidence = dataObj.value("confidence").toString();
    // QString language   = dataObj.value("language").toString(); // available for display

    // Emit attribution for found lyrics (required by Unison terms).
    // API returns submitter object with displayName; avatarUrl as fallback for submitterUrl.
    QJsonObject submitter = dataObj.value("submitter").toObject();
    QString submitterName = submitter.value("displayName").toString();
    QString submitterUrl  = submitter.value("avatarUrl").toString();
    // Construct a rough Unison page URL from song/artist if no direct url field.
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
        // TTML — extract plain text, keep TTML as synced
        plain = extractPlain(lyricsText, "ttml");
        synced = lyricsText;  // keep raw TTML as synced format
    } else if (format == "lrc") {
        // LRC — strip timestamps for plain, keep raw as synced
        static QRegularExpression lre(R"(\[\d{2}:\d{2}\.\d{2}\]|\[\d{2}:\d{2}\])");
        plain = QString(lyricsText).remove(lre).trimmed();
        synced = lyricsText;
    } else {
        // Plain text
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
