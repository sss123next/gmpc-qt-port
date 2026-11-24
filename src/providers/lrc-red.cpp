/**
 * @file lrc-red.cpp
 * @brief lrc.red synchronized lyrics provider implementation.
 *
 * Two-step lookup: search by artist+title → find ISRC → download TTML/LRC.
 * TTML is preferred (word/syllable sync); LRC is fallback (line sync).
 */

#include "lrc-red.h"

#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QDebug>

// ── Constants ──────────────────────────────────────────────────────────────

static const char *kApiBase     = "https://lrc.red";
static const char *kUserAgent   = "gmpc-qt/0.4 (https://github.com/ssss/gmpc-qt; sss@dark-alexandr.net)";
static const int   kDurationTolerance = 3;  // seconds ±

// ── Constructor ─────────────────────────────────────────────────────────────

LrcRedProvider::LrcRedProvider(QObject *parent)
    : QObject(parent), dl_(new AsyncDownload(this))
{
    dl_->setTimeout(5000, 10000);  // 5s connect, 10s overall for lyrics
}

// ── Public API ─────────────────────────────────────────────────────────────

void LrcRedProvider::fetchLyrics(const QString &artist, const QString &track,
                                 int durationSec)
{
    searchedArtist_ = artist;
    searchedTrack_ = track;
    searchedDuration_ = durationSec;

    QUrl url(QStringLiteral("%1/search.json").arg(kApiBase));
    QUrlQuery q;
    q.addQueryItem("q", QStringLiteral("%1 %2").arg(artist, track));
    url.setQuery(q);

    dl_->get(url, [this](bool ok, const QByteArray &data) {
        onSearchReply(ok, data);
    });
}

// ── Search reply ───────────────────────────────────────────────────────────

void LrcRedProvider::onSearchReply(bool ok, const QByteArray &data)
{
    if (!ok) { emit error(tr("lrc.red: network error")); return; }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) { emit error(tr("lrc.red: invalid JSON")); return; }

    QJsonArray hits = doc.object().value("hits").toArray();
    if (hits.isEmpty()) { emit notFound(); return; }

    // Find best match by title similarity and duration tolerance
    QString bestIsrc;
    int    bestScore = 0;
    const QString qTitle = searchedTrack_.toLower().trimmed();
    const QString qArtist = searchedArtist_.toLower().trimmed();

    for (const auto &item : hits) {
        QJsonObject hit = item.toObject();
        QString title  = hit.value("title").toString().toLower().trimmed();
        QString artist = hit.value("artist").toString().toLower().trimmed();
        double  dur    = hit.value("duration").toDouble();
        QString isrc   = hit.value("isrc").toString();

        if (isrc.isEmpty())
            continue;

        // Title must contain the query title (or vice versa)
        if (!title.contains(qTitle) && !qTitle.contains(title))
            continue;

        // Artist should match loosely
        int score = (artist == qArtist) ? 3
                  : (artist.contains(qArtist) || qArtist.contains(artist)) ? 2
                  : 0;

        // Duration tolerance
        if (searchedDuration_ > 0) {
            if (qAbs(dur - searchedDuration_) <= kDurationTolerance)
                score += 2;
            else if (qAbs(dur - searchedDuration_) <= kDurationTolerance * 2)
                score += 1;
            // else no bonus but still eligible
        }

        if (score > bestScore) {
            bestScore = score;
            bestIsrc  = isrc;
        }
    }

    if (bestIsrc.isEmpty()) { emit notFound(); return; }

    // Fetch track info to get TTML/LRC URLs
    QUrl trackUrl(QStringLiteral("%1/s/%2.json").arg(kApiBase, bestIsrc));
    dl_->get(trackUrl, [this](bool ok2, const QByteArray &data2) {
        onTrackReply(ok2, data2);
    });
}

// ── Track reply ────────────────────────────────────────────────────────────

void LrcRedProvider::onTrackReply(bool ok, const QByteArray &data)
{
    if (!ok) { emit error(tr("lrc.red: network error (track)")); return; }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) { emit error(tr("lrc.red: invalid track JSON")); return; }

    QJsonObject obj = doc.object();
    QJsonObject files = obj.value("files").toObject();

    // Prefer TTML (word/syllable sync), fall back to LRC (line sync)
    QString ttmlUrl = files.value("ttml").toString();
    QString lrcUrl  = files.value("lrc").toString();

    if (!ttmlUrl.isEmpty()) {
        dl_->get(QUrl(ttmlUrl), [this](bool ok3, const QByteArray &data3) {
            onLyricsReply(ok3, data3);
        });
    } else if (!lrcUrl.isEmpty()) {
        dl_->get(QUrl(lrcUrl), [this](bool ok3, const QByteArray &data3) {
            onLyricsReply(ok3, data3);
        });
    } else {
        emit notFound();
    }
}

// ── Lyrics reply ───────────────────────────────────────────────────────────

void LrcRedProvider::onLyricsReply(bool ok, const QByteArray &data)
{
    if (!ok || data.isEmpty()) { emit notFound(); return; }

    QString raw = QString::fromUtf8(data);

    // Detect format: if it starts with <tt or <?xml, it's TTML
    if (raw.trimmed().startsWith('<')) {
        // TTML format
        QString plain = ttmlToPlain(raw);
        if (plain.isEmpty()) { emit notFound(); return; }
        emit lyricsFound(plain, raw);  // plain text + raw TTML as synced
    } else {
        // LRC format or plain text
        // If it contains [MM:SS.xx] patterns, it's LRC (synced)
        static QRegularExpression lre(R"(\[\d{2}:\d{2}\.\d{2}\])");
        if (lre.match(raw).hasMatch()) {
            // Strip timestamps for plain version
            QString plain = raw;
            plain.remove(lre);
            plain = plain.trimmed();
            emit lyricsFound(plain, raw);  // plain + raw LRC as synced
        } else {
            // Plain text
            emit lyricsFound(raw, QString());
        }
    }
}

// ── TTML → Plain text ────────────────────────────────────────────────────

QString LrcRedProvider::ttmlToPlain(const QString &ttml)
{
    // Simple TTML parser: extract text between <span>...</span> tags
    // and join lines from <p> elements.

    // First, remove XML declaration if present
    QString text = ttml;
    text.remove(QRegularExpression("<\\?xml[^>]*\\?>"));
    text.remove(QRegularExpression("<\\?[^>]*\\?>"));

    // Replace <br/> with newlines
    text.replace(QRegularExpression("<br\\s*/?>"), "\n");

    // Remove all tags, keep content
    text.remove(QRegularExpression("<[^>]*>"));

    // Decode XML entities
    text.replace("&amp;", "&");
    text.replace("&lt;", "<");
    text.replace("&gt;", ">");
    text.replace("&quot;", "\"");
    text.replace("&apos;", "'");

    // Collapse multiple whitespace/newlines
    text = text.trimmed();
    text.replace(QRegularExpression("\\n{3,}"), "\n\n");

    return text;
}
