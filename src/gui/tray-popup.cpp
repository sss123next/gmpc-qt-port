#include "tray-popup.h"
#include "core/connection.h"
#include "core/playlist-model.h"
#include "metadata/metadata-manager.h"

#include <QApplication>
#include <QScreen>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStyle>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>
#include <QDebug>

// ── Geometry constants ──────────────────────────────────────────────────────

static constexpr int kCoverPx       = 80;   ///< Cover image side (px).
static constexpr int kMaxPopupW     = 400;  ///< Maximum popup width.
static constexpr int kRadius        = 8;    ///< Corner radius for the background.
static constexpr int kMinPopupW     = 260;  ///< Minimum popup width.
static constexpr int kPopupMargin   = 8;    ///< Internal margin around content.

// ── Constructor ─────────────────────────────────────────────────────────────

TrayPopup::TrayPopup(QWidget *parent)
    : QWidget(parent)
{
    // Frameless window, always on top, no taskbar entry.
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint | Qt::ToolTip);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_OpaquePaintEvent, false);

    autoHideTimer_ = new QTimer(this);
    autoHideTimer_->setSingleShot(true);
    connect(autoHideTimer_, &QTimer::timeout, this, &QWidget::hide);

    createLayout();
    applyStyle();
}

// ── Layout construction ─────────────────────────────────────────────────────

void TrayPopup::createLayout()
{
    // ── Container widget: draws the rounded-rect background via paintEvent.
    container_ = new QWidget(this);
    container_->setAttribute(Qt::WA_TranslucentBackground, false);

    auto *rootLay = new QHBoxLayout(container_);
    rootLay->setContentsMargins(kPopupMargin, kPopupMargin,
                                kPopupMargin, kPopupMargin);
    rootLay->setSpacing(10);

    // ── Left: cover art ───────────────────────────────────────────────
    coverLabel_ = new QLabel(container_);
    coverLabel_->setFixedSize(kCoverPx, kCoverPx);
    coverLabel_->setAlignment(Qt::AlignCenter);
    coverLabel_->setText(QStringLiteral("♪"));
    coverLabel_->setStyleSheet(QStringLiteral(
        "font-size: 28px; border: none;"));
    coverLabel_->setVisible(showCover_);

    // ── Right: text info + buttons ──────────────────────────────────
    auto *rightCol = new QVBoxLayout;
    rightCol->setSpacing(4);

    titleLabel_ = new QLabel(container_);
    titleLabel_->setWordWrap(false);
    titleLabel_->setTextFormat(Qt::PlainText);
    titleLabel_->setSizePolicy(QSizePolicy::MinimumExpanding,
                               QSizePolicy::Preferred);
    titleLabel_->setStyleSheet(QStringLiteral("font-weight: bold; border: none;"));
    titleLabel_->setTextInteractionFlags(Qt::NoTextInteraction);

    artistLabel_ = new QLabel(container_);
    artistLabel_->setWordWrap(false);
    artistLabel_->setTextFormat(Qt::PlainText);
    artistLabel_->setStyleSheet(QStringLiteral("border: none;"));

    albumLabel_ = new QLabel(container_);
    albumLabel_->setWordWrap(false);
    albumLabel_->setTextFormat(Qt::PlainText);
    albumLabel_->setStyleSheet(QStringLiteral("border: none;"));

    rightCol->addWidget(titleLabel_);
    rightCol->addWidget(artistLabel_);
    rightCol->addWidget(albumLabel_);
    rightCol->addStretch();

    // ── Transport buttons ────────────────────────────────────────────
    buttonBar_ = new QWidget(container_);
    auto *btnLay = new QHBoxLayout(buttonBar_);
    btnLay->setContentsMargins(0, 4, 0, 0);
    btnLay->setSpacing(4);

    auto mkBtn = [&](const QStyle::StandardPixmap sp) {
        auto *b = new QPushButton(buttonBar_);
        b->setIcon(style()->standardIcon(sp));
        b->setFixedSize(24, 24);
        b->setFlat(true);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };

    prevBtn_ = mkBtn(QStyle::SP_MediaSkipBackward);
    playBtn_ = mkBtn(QStyle::SP_MediaPlay);
    stopBtn_ = mkBtn(QStyle::SP_MediaStop);
    nextBtn_ = mkBtn(QStyle::SP_MediaSkipForward);

    btnLay->addWidget(prevBtn_);
    btnLay->addWidget(playBtn_);
    btnLay->addWidget(stopBtn_);
    btnLay->addWidget(nextBtn_);

    connect(prevBtn_, &QPushButton::clicked, this, &TrayPopup::previousClicked);
    connect(playBtn_, &QPushButton::clicked, this, &TrayPopup::playPauseClicked);
    connect(stopBtn_, &QPushButton::clicked, this, &TrayPopup::stopClicked);
    connect(nextBtn_, &QPushButton::clicked, this, &TrayPopup::nextClicked);

    rightCol->addWidget(buttonBar_);
    buttonBar_->setVisible(showButtons_);

    // ── Assemble ─────────────────────────────────────────────────────
    rootLay->addWidget(coverLabel_);
    rootLay->addLayout(rightCol, 1);

    // The outer TrayPopup widget just wraps container_ with margin for shadow.
    auto *outerLay = new QVBoxLayout(this);
    outerLay->setContentsMargins(0, 0, 0, 0);
    outerLay->addWidget(container_);
}

// ── Metadata Manager ────────────────────────────────────────────────────────

void TrayPopup::setMetadataManager(MetadataManager *mgr)
{
    mgr_ = mgr;
    if (mgr_) {
        connect(mgr_, &MetadataManager::coverReady, this,
                [this](const QString &artist, const QString &album,
                       const QByteArray &data) {
                    qDebug() << "TrayPopup::coverReady" << artist << album
                             << "lastArtist_" << lastArtist_ << "lastAlbum_" << lastAlbum_
                             << "data.size()" << data.size();
                    if (artist != lastArtist_ || album != lastAlbum_) {
                        qDebug() << "  → stale, ignoring";
                        return;
                    }
                    QPixmap pm;
                    if (pm.loadFromData(data)) {
                        qDebug() << "  → setting pixmap";
                        QPixmap scaled = pm.scaled(
                            kCoverPx, kCoverPx, Qt::KeepAspectRatio,
                            Qt::SmoothTransformation);
                        coverLabel_->setPixmap(scaled);
                        coverLabel_->setText({});
                    } else {
                        qDebug() << "  → loadFromData FAILED";
                    }
                });
        connect(mgr_, &MetadataManager::coverNotFound, this,
                [this](const QString &, const QString &) {
                    coverLabel_->setPixmap(QPixmap());
                    coverLabel_->setText(QStringLiteral("♪"));
                });
    }
}

// ── Show / Update ───────────────────────────────────────────────────────────

void TrayPopup::showSong(const SongEntry *song)
{
    if (!song) {
        clear();
        return;
    }

    // Update text labels.
    titleLabel_->setText(song->title.isEmpty()
                         ? song->uri
                         : song->title);
    artistLabel_->setText(song->artist);
    albumLabel_->setText(song->album);

    // Update cover if artist/album changed.
    if (song->artist != lastArtist_ || song->album != lastAlbum_) {
        qDebug() << "TrayPopup::showSong cover changed"
                 << "artist:" << lastArtist_ << "→" << song->artist
                 << "album:" << lastAlbum_ << "→" << song->album;
        lastArtist_ = song->artist;
        lastAlbum_  = song->album;
        if (mgr_ && !song->artist.isEmpty() && !song->album.isEmpty()) {
            coverLabel_->setText(QStringLiteral("⋯"));
            mgr_->requestCover(song->artist, song->album, song->uri);
        } else {
            coverLabel_->setPixmap(QPixmap());
            coverLabel_->setText(QStringLiteral("♪"));
        }
    } else {
        qDebug() << "TrayPopup::showSong cover unchanged — skipping fetch";
    }

    // Resize to fit content, clamp to reasonable width.
    container_->adjustSize();
    int w = qMin(container_->minimumSizeHint().width(), kMaxPopupW);
    w = qMax(w, qMin(kMinPopupW, container_->minimumSizeHint().width()));
    container_->resize(w, container_->minimumSizeHint().height());
    adjustSize();

    positionOnScreen();
    startAutoHide();

    if (!isVisible())
        show();
}

void TrayPopup::clear()
{
    hide();
    stopAutoHide();
    titleLabel_->clear();
    artistLabel_->clear();
    albumLabel_->clear();
    coverLabel_->setPixmap(QPixmap());
    coverLabel_->setText(QStringLiteral("♪"));
    lastArtist_.clear();
    lastAlbum_.clear();
}

// ── Positioning ─────────────────────────────────────────────────────────────

void TrayPopup::positionOnScreen()
{
    const QScreen *screen = QApplication::primaryScreen();
    if (!screen) {
        move(100, 100);
        return;
    }
    const QRect g = screen->availableGeometry();
    const int pw = frameGeometry().width();
    const int ph = frameGeometry().height();
    move((g.width()  - pw) / 2,
         (g.height() - ph) / 2);
}

// ── Auto-hide ───────────────────────────────────────────────────────────────

void TrayPopup::startAutoHide()
{
    stopAutoHide();
    if (timeoutSec_ > 0)
        autoHideTimer_->start(timeoutSec_ * 1000);
}

void TrayPopup::stopAutoHide()
{
    autoHideTimer_->stop();
}

// ── Paint: rounded-rect background ──────────────────────────────────────────

void TrayPopup::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRect cr = container_->geometry();

    QPainterPath path;
    path.addRoundedRect(QRectF(cr), kRadius, kRadius);

    p.fillPath(path, QColor(presetBg_));
    p.setPen(QPen(QColor(presetFg_), 1));
    p.drawPath(path);
}

// ── Style presets ─────────────────────────────────────────────────────────

void TrayPopup::applyStyle()
{
    presetBg_ = presetFg_ = presetBtnBg_ = presetBtnFg_ = presetCoverBg_ = {};

    if (style_ == Dark) {
        presetBg_      = QStringLiteral("#2b2b2b");
        presetFg_      = QStringLiteral("#e0e0e0");
        presetBtnBg_   = QStringLiteral("#3c3c3c");
        presetBtnFg_   = QStringLiteral("#ffffff");
        presetCoverBg_ = QStringLiteral("#1a1a1a");
    } else {
        presetBg_      = QStringLiteral("#f0f0f0");
        presetFg_      = QStringLiteral("#222222");
        presetBtnBg_   = QStringLiteral("#e0e0e0");
        presetBtnFg_   = QStringLiteral("#222222");
        presetCoverBg_ = QStringLiteral("#ffffff");
    }

    container_->setAttribute(Qt::WA_TranslucentBackground, true);

    const QString labelStyle = QStringLiteral(
        "color: %1; background: transparent; border: none; padding: 0;")
        .arg(presetFg_);
    titleLabel_->setStyleSheet(labelStyle + QStringLiteral(" font-weight: bold;"));
    artistLabel_->setStyleSheet(labelStyle);
    albumLabel_->setStyleSheet(labelStyle);

    coverLabel_->setStyleSheet(QStringLiteral(
        "background: %1; border: none; font-size: 28px; color: %2;")
        .arg(presetCoverBg_, presetFg_));

    const QString btnStyle = QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none;"
        "  border-radius: 3px; padding: 2px; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:pressed { background: %4; }")
        .arg(presetBtnBg_, presetBtnFg_,
             style_ == Dark ? "#555555" : "#cccccc",
             style_ == Dark ? "#777777" : "#aaaaaa");
    for (auto *b : {prevBtn_, playBtn_, stopBtn_, nextBtn_}) {
        if (b)
            b->setStyleSheet(btnStyle);
    }

    update();
}

// ── Setters ─────────────────────────────────────────────────────────────────

void TrayPopup::setStylePreset(StylePreset preset)
{
    style_ = preset;
    applyStyle();
}

void TrayPopup::setTimeoutSec(int sec)
{
    timeoutSec_ = qMax(0, sec);
}

void TrayPopup::setShowCover(bool show)
{
    showCover_ = show;
    coverLabel_->setVisible(show);
    adjustSize();
}

void TrayPopup::setShowButtons(bool show)
{
    showButtons_ = show;
    buttonBar_->setVisible(show);
    adjustSize();
}
