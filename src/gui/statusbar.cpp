#include "statusbar.h"

#include <QHBoxLayout>
#include <QStringList>

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

    QString text;
    if (s.state == MPD_STATE_PLAY) {
        // Format sample rate: 44100 → "44.1kHz", 48000 → "48kHz".
        QString sr;
        if (s.sampleRate > 0) {
            if (s.sampleRate % 1000 == 0)
                sr = QStringLiteral("%1kHz").arg(s.sampleRate / 1000);
            else
                sr = QStringLiteral("%1kHz").arg(s.sampleRate / 1000.0, 0, 'f', 1);
        }
        // Format bit depth.
        QString depth;
        if (s.bits > 0 && s.bits <= 32)
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
        if (!sr.isEmpty())   parts << sr;
        if (!depth.isEmpty()) parts << depth;
        if (!br.isEmpty())   parts << br;
        const QString fmt = parts.join(QLatin1Char(' '));

        if (fmt.isEmpty())
            text = tr("Playing");
        else
            text = tr("Playing — %1").arg(fmt);
    } else if (s.state == MPD_STATE_PAUSE)
        text = tr("Paused");
    else
        text = tr("Stopped");
    infoLabel_->setText(text);
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
