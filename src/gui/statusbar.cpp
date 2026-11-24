#include "statusbar.h"

#include <QHBoxLayout>
#include <QStringList>

// Внешняя видимость для числа: знакомая каждому значение 2¹⁶-1.
// MPD queue — uint32; если понадобится, переключим на %1.
static QString fmtQueuePos(int pos, int len)
{
    // pos == -1 при остановленном — не показываем ничего.
    if (pos < 0 || len <= 0)
        return {};
    return QStringLiteral("%1 / %2").arg(pos + 1).arg(len);
}

StatusBar::StatusBar(QWidget *parent)
    : QWidget(parent)
    , msgTimer_(new QTimer(this))
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(4, 0, 4, 0);

    connLabel_ = new QLabel(tr("Disconnected"), this);
    connLabel_->setStyleSheet("color: #cc0000;");

    infoLabel_ = new QLabel(this);

    lay->addWidget(connLabel_, 0);
    lay->addStretch();
    lay->addWidget(infoLabel_, 0);

    setMaximumHeight(24);

    msgTimer_->setSingleShot(true);
    connect(msgTimer_, &QTimer::timeout, this, [this] {
        showingMessage_ = false;
        infoLabel_->clear();
    });
}

void StatusBar::updateStatus(const Connection::Status &s)
{
    // Не перебивать временное сообщение статусом проигрывания.
    if (showingMessage_)
        return;

    const QString qp = fmtQueuePos(s.songPos, s.queueLength);

    QString stateText;
    if (s.state == MPD_STATE_PLAY) {
        // Format sample rate: 44100 → "44.1kHz", 48000 → "48kHz".
        QString sr;
        if (s.sampleRate > 0) {
            if (s.sampleRate % 1000 == 0)
                sr = QStringLiteral("%1kHz").arg(s.sampleRate / 1000);
            else
                sr = QStringLiteral("%1kHz").arg(s.sampleRate / 1000.0, 0, 'f', 1);
        }
        // Format bit depth (handle special sample formats).
        QString depth;
        if (s.bits == MPD_SAMPLE_FORMAT_FLOAT)
            depth = QStringLiteral("Float");
        else if (s.bits == MPD_SAMPLE_FORMAT_DSD)
            depth = QStringLiteral("DSD");
        else if (s.bits > 0 && s.bits <= 32)
            depth = QStringLiteral("%1bit").arg(s.bits);
        // Format bitrate: ≥1000 → Mbps, else kbps.
        QString br;
        if (s.bitRate > 0) {
            if (s.bitRate >= 1000)
                br = QStringLiteral("%1Mbps").arg(s.bitRate / 1000.0, 0, 'f', 1);
            else
                br = QStringLiteral("%1kbps").arg(s.bitRate);
        }

        QStringList parts;
        if (!format_.isEmpty())  parts << format_;
        if (!sr.isEmpty())       parts << sr;
        if (!depth.isEmpty())    parts << depth;
        if (!br.isEmpty())       parts << br;
        const QString fmt = parts.join(QLatin1Char(' '));

        if (fmt.isEmpty())
            stateText = tr("Playing");
        else
            stateText = tr("Playing — %1").arg(fmt);
    } else if (s.state == MPD_STATE_PAUSE)
        stateText = tr("Paused");
    else
        stateText = tr("Stopped");

    if (qp.isEmpty())
        infoLabel_->setText(stateText);
    else
        infoLabel_->setText(QStringLiteral("%1  %2").arg(qp, stateText));
}

void StatusBar::setFormat(const QString &fmt)
{
    format_ = fmt;
}

void StatusBar::showConnected()
{
    connLabel_->setText(tr("Connected"));
    connLabel_->setStyleSheet("color: #006600;");
}

void StatusBar::showDisconnected()
{
    connLabel_->setText(tr("Disconnected"));
    connLabel_->setStyleSheet("color: #cc0000;");
}

void StatusBar::showMessage(const QString &msg, int timeoutMs)
{
    infoLabel_->setText(msg);
    showingMessage_ = true;
    msgTimer_->start(timeoutMs);
}
