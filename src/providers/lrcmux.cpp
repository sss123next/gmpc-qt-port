/**
 * @file lrcmux.cpp
 * @brief Lrcmux aggregated lyrics provider implementation.
 *
 * Single endpoint GET request, JSON response with lines[].
 * Builds LRC-style synced lyrics from word/line timestamps.
 */

#include "lrcmux.h"

#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

// ── Constants ──────────────────────────────────────────────────────────────

static const char *kApiBase   = "https://api.lrcmux.dev/get";
static const char *kUserAgent = "gmpc-qt/0.3 (https://github.com/ssss/gmpc-qt; sss@dark-alexandr.net)";

// ── Constructor ─────────────────────────────────────────────────────────────

LrcmuxProvider::LrcmuxProvider(QObject *parent)
    : QObject(parent), dl_(new AsyncDownload(this))
{
    dl_->setTimeout(5000, 10000);  // 5s connect, 10s overall for lyrics
}

// ── Public API ─────────────────────────────────────────────────────────────

void LrcmuxProvider::fetchLyrics(const QString &artist, const QString &track,
                                 int durationSec)
{
    searchedArtist_ = artist;
    searchedTrack_ = track;
    searchedDuration_ = durationSec;

    QUrl url(kApiBase);
    QUrlQuery q;
    q.addQueryItem("artist", artist);
    q.addQueryItem("title", track);
    q.addQueryItem("format", "json");
    if (durationSec > 0)
        q.addQueryItem("duration", QString::number(durationSec));
    url.setQuery(q);

    dl_->get(url, [this](bool ok, const QByteArray &data) {
        onReply(ok, data);
    });
}

// ── Response handler ───────────────────────────────────────────────────────

void LrcmuxProvider::onReply(bool ok, const QByteArray &data)
{
    if (!ok) { emit error(tr("Lrcmux: network error")); return; }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) { emit error(tr("Lrcmux: invalid JSON")); return; }

    QJsonObject root = doc.object();

    // Check for instrumental or empty
    QJsonObject meta = root.value("meta").toObject();
    if (meta.value("instrumental").toBool(false)) {
        emit lyricsFound(QStringLiteral("[Instrumental]"), QString());
        return;
    }

    QJsonArray lines = root.value("lines").toArray();
    if (lines.isEmpty()) { emit notFound(); return; }

    // Build plain text (join all line texts)
    QStringList plainParts;
    for (const auto &line : lines) {
        plainParts << line.toObject().value("text").toString();
    }
    QString plain = plainParts.join("\n").trimmed();
    if (plain.isEmpty()) { emit notFound(); return; }

    // Build synced lyrics if timing available
    QString level = meta.value("level").toString();
    QString synced;
    if (level == "word" || level == "line") {
        synced = buildLrc(lines, level);
    }

    emit lyricsFound(plain, synced);
}

// ── LRC builder ──────────────────────────────────────────────────────────

QString LrcmuxProvider::buildLrc(const QJsonArray &lines, const QString &level)
{
    QStringList result;
    for (const auto &line : lines) {
        QJsonObject l = line.toObject();
        int startMs = l.value("start").toInt(0);
        int endMs   = l.value("end").toInt(0);
        QString text = l.value("text").toString();

        if (text.trimmed().isEmpty())
            continue;

        // Format: [MM:SS.xx]
        int min = startMs / 60000;
        int sec = (startMs % 60000) / 1000;
        int hun = (startMs % 1000) / 10;

        QString lrcLine;
        if (level == "word" && l.contains("words")) {
            // For word-level, use individual word timestamps
            QJsonArray words = l.value("words").toArray();
            QStringList wordParts;
            for (const auto &w : words) {
                QJsonObject wo = w.toObject();
                int ws = wo.value("start").toInt();
                int we = wo.value("end").toInt();
                QString wt = wo.value("text").toString();
                if (!wt.isEmpty()) {
                    int wmin = ws / 60000, wsec = (ws % 60000) / 1000, whun = (ws % 1000) / 10;
                    wordParts << QStringLiteral("<%1:%2.%3>%4")
                        .arg(wmin, 2, 10, QChar('0'))
                        .arg(wsec, 2, 10, QChar('0'))
                        .arg(whun, 2, 10, QChar('0'))
                        .arg(wt);
                }
            }
            if (!wordParts.isEmpty()) {
                lrcLine = QStringLiteral("[%1:%2.%3]%4")
                    .arg(min, 2, 10, QChar('0'))
                    .arg(sec, 2, 10, QChar('0'))
                    .arg(hun, 2, 10, QChar('0'))
                    .arg(wordParts.join(""));
            } else {
                lrcLine = QStringLiteral("[%1:%2.%3]%4")
                    .arg(min, 2, 10, QChar('0'))
                    .arg(sec, 2, 10, QChar('0'))
                    .arg(hun, 2, 10, QChar('0'))
                    .arg(text);
            }
        } else {
            lrcLine = QStringLiteral("[%1:%2.%3]%4")
                .arg(min, 2, 10, QChar('0'))
                .arg(sec, 2, 10, QChar('0'))
                .arg(hun, 2, 10, QChar('0'))
                .arg(text);
        }
        result << lrcLine;
    }
    return result.join("\n");
}
