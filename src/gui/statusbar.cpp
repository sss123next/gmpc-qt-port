#include "statusbar.h"

#include <QHBoxLayout>

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
    if (s.state == MPD_STATE_PLAY)
        text = tr("Playing — %1 kbps").arg(s.bitRate);
    else if (s.state == MPD_STATE_PAUSE)
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
