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
#include <QEnterEvent>
#include <QStyleOption>
#include <QWindow>

// ── Geometry constants ──────────────────────────────────────────────────────

static constexpr int kCoverPx       = 80;   ///< Cover image side (px).
static constexpr int kMaxPopupW     = 400;  ///< Maximum popup width.
static constexpr int kRadius        = 8;    ///< Corner radius for the background.
static constexpr int kMinPopupW     = 260;  ///< Minimum popup width.
static constexpr int kPopupMargin   = 8;    ///< Internal margin around content.
static constexpr int kIconGap       = 4;    ///< Gap between popup and tray icon.

// ── Constructor ─────────────────────────────────────────────────────────────

TrayPopup::TrayPopup(QWidget *parent)
    : QWidget(parent)
{
    // Frameless popup window, always on top.  Qt::ToolTip avoids taskbar entry
    // and keeps the window manager from stealing focus from the main window.
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint | Qt::ToolTip);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    // Mouse tracking for enter/leave while cursor is over the popup.
    setMouseTracking(true);

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
    // Ellipsis when too long.
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
        // Borderless: style inherits from container.
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
                    // Ignore stale responses for older songs.
                    if (artist != lastArtist_ || album != lastAlbum_)
                        return;
                    QPixmap pm;
                    if (pm.loadFromData(data)) {
                        QPixmap scaled = pm.scaled(
                            kCoverPx, kCoverPx, Qt::KeepAspectRatio,
                            Qt::SmoothTransformation);
                        coverLabel_->setPixmap(scaled);
                        coverLabel_->setText({});
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

void TrayPopup::showSong(const SongEntry *song, const QPoint &iconPos)
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
    const QString albumText = song->album;
    albumLabel_->setText(albumText);

    // Update cover if artist/album changed.
    if (song->artist != lastArtist_ || song->album != lastAlbum_) {
        lastArtist_ = song->artist;
        lastAlbum_  = song->album;
        if (mgr_ && !song->artist.isEmpty() && !song->album.isEmpty()) {
            coverLabel_->setText(QStringLiteral("⋯"));
            mgr_->requestCover(song->artist, song->album, song->uri);
        } else {
            coverLabel_->setPixmap(QPixmap());
            coverLabel_->setText(QStringLiteral("♪"));
        }
    }

    // Resize to fit content, clamp to reasonable width.
    container_->adjustSize();
    int w = qMin(container_->minimumSizeHint().width(), kMaxPopupW);
    w = qMax(w, qMin(kMinPopupW, container_->minimumSizeHint().width()));
    container_->resize(w, container_->minimumSizeHint().height());
    adjustSize();

    reposition(iconPos);
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

void TrayPopup::reposition(const QPoint &iconPos)
{
    const QScreen *screen = QApplication::screenAt(iconPos);
    if (!screen)
        screen = QApplication::primaryScreen();
    const QRect screenGeom = screen ? screen->availableGeometry()
                                    : QRect(0, 0, 1920, 1080);

    const int pw = frameGeometry().width();
    const int ph = frameGeometry().height();

    int x = 0, y = 0;

    switch (position_) {
    case AtCursor:
        x = QCursor::pos().x();
        y = QCursor::pos().y() + kIconGap;
        break;
    case NearTrayTopLeft:
        x = iconPos.x() + kIconGap + xOffset_;
        y = iconPos.y() - ph - kIconGap + yOffset_;
        break;
    case NearTrayTopRight:
        x = iconPos.x() - pw - kIconGap + xOffset_;
        y = iconPos.y() - ph - kIconGap + yOffset_;
        break;
    case NearTrayBottomLeft:
        x = iconPos.x() + kIconGap + xOffset_;
        y = iconPos.y() + kIconGap + yOffset_;
        break;
    case NearTrayBottomRight:
        x = iconPos.x() - pw - kIconGap + xOffset_;
        y = iconPos.y() + kIconGap + yOffset_;
        break;
    }

    // Clamp to screen.
    x = qBound(screenGeom.left(), x, screenGeom.right() - pw);
    y = qBound(screenGeom.top(),  y, screenGeom.bottom() - ph);

    move(x, y);
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

void TrayPopup::enterEvent(QEnterEvent * /*event*/)
{
    // Cursor entered popup — postpone auto-hide.
    stopAutoHide();
}

void TrayPopup::leaveEvent(QEvent * /*event*/)
{
    // Cursor left popup — restart auto-hide.
    startAutoHide();
}

// ── Paint: rounded-rect background ──────────────────────────────────────────

void TrayPopup::paintEvent(QPaintEvent * /*event*/)
{
    // Only paint the outer translucent area and container background.
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // The container_ fills the widget area; we just draw its background
    // with rounded corners and a border.
    const QRect cr = container_->geometry();

    QPainterPath path;
    path.addRoundedRect(QRectF(cr), kRadius, kRadius);

    p.setClipPath(path);
    p.fillPath(path, container_->palette().window());
    p.setPen(QPen(container_->palette().windowText().color(), 1));
    p.drawPath(path);
}

// ── Style presets ─────────────────────────────────────────────────────────

void TrayPopup::applyStyle()
{
    QString bg, fg, btnBg, btnFg, coverBg;

    if (style_ == Dark) {
        bg     = QStringLiteral("#2b2b2b");
        fg     = QStringLiteral("#e0e0e0");
        btnBg  = QStringLiteral("#3c3c3c");
        btnFg  = QStringLiteral("#ffffff");
        coverBg= QStringLiteral("#1a1a1a");
    } else {
        bg     = QStringLiteral("#f0f0f0");
        fg     = QStringLiteral("#222222");
        btnBg  = QStringLiteral("#e0e0e0");
        btnFg  = QStringLiteral("#222222");
        coverBg= QStringLiteral("#ffffff");
    }

    // Container background with rounded corners.
    container_->setStyleSheet(QStringLiteral(
        "QWidget { background: %1; }").arg(bg));

    // Labels.
    const QString labelStyle = QStringLiteral(
        "color: %1; background: transparent; border: none; padding: 0;").arg(fg);
    titleLabel_->setStyleSheet(labelStyle + QStringLiteral(" font-weight: bold;"));
    artistLabel_->setStyleSheet(labelStyle);
    albumLabel_->setStyleSheet(labelStyle);

    // Cover background.
    coverLabel_->setStyleSheet(QStringLiteral(
        "background: %1; border: none; font-size: 28px; color: %2;")
        .arg(coverBg, fg));

    // Transport buttons.
    const QString btnStyle = QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none;"
        "  border-radius: 3px; padding: 2px; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:pressed { background: %4; }")
        .arg(btnBg, btnFg,
             style_ == Dark ? "#555555" : "#cccccc",
             style_ == Dark ? "#777777" : "#aaaaaa");
    for (auto *b : {prevBtn_, playBtn_, stopBtn_, nextBtn_}) {
        if (b)
            b->setStyleSheet(btnStyle);
    }

    // Update background.
    update();
}

// ── Setters ─────────────────────────────────────────────────────────────────

void TrayPopup::setPosition(Position pos)
{
    position_ = pos;
}

void TrayPopup::setStylePreset(StylePreset preset)
{
    style_ = preset;
    applyStyle();
}

void TrayPopup::setTimeoutSec(int sec)
{
    timeoutSec_ = qMax(1, sec);
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

void TrayPopup::setOffsets(int x, int y)
{
    xOffset_ = x;
    yOffset_ = y;
}
