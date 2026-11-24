/**
 * @file mini-player.cpp
 * @brief Borderless mini-player window — stylesheet background,
 *        CoverWidget for cover art, draggable, position remembered.
 */

#include "mini-player.h"
#include "widgets/cover-widget.h"
#include "widgets/cover-viewer.h"
#include "metadata/metadata-manager.h"
#include "core/playlist-model.h"
#include "core/config.h"

#include <QApplication>
#include <QScreen>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStyle>
#include <QMouseEvent>
#include <QHideEvent>
#include <QDebug>

// ── Geometry constants ──────────────────────────────────────────────────────

static constexpr int kCoverPx     = 80;   ///< Cover image side (px).
static constexpr int kRadius      = 8;    ///< Corner radius for the popup.
static constexpr int kMargin      = 10;   ///< Internal margin.

// ── Constructor ─────────────────────────────────────────────────────────────

MiniPlayer::MiniPlayer(QWidget *parent)
    : QWidget(parent)
{
    // ToolTip flag tells the WM (especially tiling ones like Awesome) to
    // NOT manage this window — no tiling, no resizing, no decorations.
    // Position is set manually and remembered across sessions.
    // On Windows, ToolTip prevents mouse events — use Tool instead.
#if defined(Q_OS_WIN)
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint);
#else
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint);
#endif
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_OpaquePaintEvent, false);

    autoHideTimer_ = new QTimer(this);
    autoHideTimer_->setSingleShot(true);
    connect(autoHideTimer_, &QTimer::timeout, this, &QWidget::hide);

    createLayout();
    applyStyle();

    // Click on cover → full-screen viewer (single instance, reused).
    connect(coverWidget_, &CoverWidget::clicked, this, [this] {
        if (coverWidget_->coverData().isEmpty())
            return;
        if (!coverViewer_)
            coverViewer_ = new CoverViewer();
        coverViewer_->show(coverWidget_->coverData());
    });
}

// ── Layout construction ─────────────────────────────────────────────────────

void MiniPlayer::createLayout()
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(kMargin, kMargin, kMargin, kMargin);
    root->setSpacing(10);

    // ── Left: CoverWidget (proven to work in NowPlaying) ──────────────
    coverWidget_ = new CoverWidget(this);
    coverWidget_->setFixedSize(kCoverPx, kCoverPx);
    coverWidget_->setCoverSize(kCoverPx);
    root->addWidget(coverWidget_);

    // ── Right: text info + buttons ──────────────────────────────────
    auto *right = new QVBoxLayout;
    right->setSpacing(3);

    titleLabel_ = new QLabel(this);
    titleLabel_->setWordWrap(false);
    titleLabel_->setTextFormat(Qt::PlainText);
    titleLabel_->setSizePolicy(QSizePolicy::Preferred,
                               QSizePolicy::Preferred);
    titleLabel_->setTextInteractionFlags(Qt::NoTextInteraction);

    artistLabel_ = new QLabel(this);
    artistLabel_->setWordWrap(false);
    artistLabel_->setTextFormat(Qt::PlainText);

    albumLabel_ = new QLabel(this);
    albumLabel_->setWordWrap(false);
    albumLabel_->setTextFormat(Qt::PlainText);

    right->addWidget(titleLabel_);
    right->addWidget(artistLabel_);
    right->addWidget(albumLabel_);
    right->addStretch();

    // ── Transport buttons ────────────────────────────────────────────
    buttonBar_ = new QWidget(this);
    auto *btnLay = new QHBoxLayout(buttonBar_);
    btnLay->setContentsMargins(0, 4, 0, 0);
    btnLay->setSpacing(4);

    auto mkBtn = [&](QStyle::StandardPixmap sp) {
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

    connect(prevBtn_, &QPushButton::clicked, this, &MiniPlayer::previousClicked);
    connect(playBtn_, &QPushButton::clicked, this, &MiniPlayer::playPauseClicked);
    connect(stopBtn_, &QPushButton::clicked, this, &MiniPlayer::stopClicked);
    connect(nextBtn_, &QPushButton::clicked, this, &MiniPlayer::nextClicked);

    right->addWidget(buttonBar_);
    buttonBar_->setVisible(showButtons_);

    root->addLayout(right, 1);

    // Always fit window to content — no manual adjustSize() needed.
    root->setSizeConstraint(QLayout::SetFixedSize);

    // Cursor hint for drag (may be disabled by config later).
    setCursor(Qt::OpenHandCursor);
}

// ── Metadata Manager ────────────────────────────────────────────────────────

void MiniPlayer::setMetadataManager(MetadataManager *mgr)
{
    mgr_ = mgr;
    if (coverWidget_)
        coverWidget_->setMetadataManager(mgr);
}

// ── Show / Update ───────────────────────────────────────────────────────────

void MiniPlayer::updateContent(const SongEntry *song)
{
    if (!song) {
        clear();
        return;
    }

    titleLabel_->setText(song->title.isEmpty() ? song->uri : song->title);
    artistLabel_->setText(song->artist);
    albumLabel_->setText(song->album);

    // Delegate cover fetching to CoverWidget (proven pipeline).
    // Only fetch when covers are enabled in the mini-player settings.
    if (showCover_ && !song->artist.isEmpty() && !song->album.isEmpty())
        coverWidget_->fetchCover(song->artist, song->album, song->uri);
    else
        coverWidget_->clear();

    // Free full-screen cover if viewer was hidden (saves ~8 MB per track).
    if (coverViewer_ && !coverViewer_->isVisible())
        coverViewer_->clear();

    // Defer positioning so QLayout::SetFixedSize has time to update
    // width()/height() after setText() above.
    QTimer::singleShot(0, this, &MiniPlayer::positionOnScreen);
    startAutoHide();
}

void MiniPlayer::showSong(const SongEntry *song)
{
    updateContent(song);
    if (!isVisible() && song)
        show();
}

void MiniPlayer::clear()
{
    hide();
    stopAutoHide();
    titleLabel_->clear();
    artistLabel_->clear();
    albumLabel_->clear();
    coverWidget_->clear();
    if (coverViewer_)
        coverViewer_->clear();
}

// ── Positioning ─────────────────────────────────────────────────────────────

void MiniPlayer::positionOnScreen()
{
    auto &cfg = Config::instance();

    // Read the latest config values into local state.
    posMode_  = static_cast<PositionMode>(cfg.miniPlayerPositionMode());
    screenIdx_ = cfg.miniPlayerScreen();
    marginX_   = cfg.miniPlayerMarginX();
    marginY_   = cfg.miniPlayerMarginY();

    const QScreen *screen = resolveScreen();
    if (!screen) { move(100, 100); return; }
    const QRect g = screen->availableGeometry();

    computeAnchor(g);
    applyAnchorPosition();
}

void MiniPlayer::computeAnchor(const QRect &g)
{
    switch (posMode_) {
    case PosTopLeft:
        anchorX_ = g.x() + marginX_;
        anchorY_ = g.y() + marginY_;
        anchorCorner_ = AnchorTopLeft;
        break;
    case PosTopRight:
        anchorX_ = g.x() + g.width() - marginX_;
        anchorY_ = g.y() + marginY_;
        anchorCorner_ = AnchorTopRight;
        break;
    case PosBottomLeft:
        anchorX_ = g.x() + marginX_;
        anchorY_ = g.y() + g.height() - marginY_;
        anchorCorner_ = AnchorBottomLeft;
        break;
    case PosBottomRight:
        anchorX_ = g.x() + g.width()  - marginX_;
        anchorY_ = g.y() + g.height() - marginY_;
        anchorCorner_ = AnchorBottomRight;
        break;
    case PosCustom: {
        const int sx = Config::instance().miniPlayerX();
        const int sy = Config::instance().miniPlayerY();
        if (sx >= 0 && sy >= 0) {
            anchorX_ = sx;
            anchorY_ = sy;
            anchorCorner_ = AnchorTopLeft;  // saved absolute pos = top-left
            break;
        }
        // fall through to centre
    }
    default:  // PosCenter
        anchorX_ = g.x() + g.width()  / 2;
        anchorY_ = g.y() + g.height() / 2;
        anchorCorner_ = AnchorCenter;
        break;
    }
}

void MiniPlayer::applyAnchorPosition()
{
    const int pw = width();
    const int ph = height();
    int x, y;
    switch (anchorCorner_) {
    case AnchorTopLeft:
        x = anchorX_;
        y = anchorY_;
        break;
    case AnchorTopRight:
        x = anchorX_ - pw;
        y = anchorY_;
        break;
    case AnchorBottomRight:
        x = anchorX_ - pw;
        y = anchorY_ - ph;
        break;
    case AnchorBottomLeft:
        x = anchorX_;
        y = anchorY_ - ph;
        break;
    case AnchorCenter:
    default:
        x = anchorX_ - pw / 2;
        y = anchorY_ - ph / 2;
        break;
    }
    move(x, y);
}

const QScreen *MiniPlayer::resolveScreen() const
{
    const auto screens = QApplication::screens();
    if (screens.isEmpty())
        return nullptr;
    if (screenIdx_ >= 0 && screenIdx_ < screens.size())
        return screens.at(screenIdx_);
    return QApplication::primaryScreen();
}

void MiniPlayer::saveGeometry()
{
    auto &cfg = Config::instance();
    cfg.setMiniPlayerPositionMode(static_cast<int>(posMode_));
    cfg.setMiniPlayerScreen(screenIdx_);
    cfg.setMiniPlayerMarginX(marginX_);
    cfg.setMiniPlayerMarginY(marginY_);
    cfg.setMiniPlayerX(x());
    cfg.setMiniPlayerY(y());
    cfg.setMiniPlayerW(width());
    cfg.setMiniPlayerH(height());
}

void MiniPlayer::restoreGeometry()
{
    auto &cfg = Config::instance();
    posMode_   = static_cast<PositionMode>(cfg.miniPlayerPositionMode());
    screenIdx_ = cfg.miniPlayerScreen();
    marginX_   = cfg.miniPlayerMarginX();
    marginY_   = cfg.miniPlayerMarginY();
    const int sw = cfg.miniPlayerW();
    const int sh = cfg.miniPlayerH();
    if (sw > 0 && sh > 0)
        resize(sw, sh);
    // Position will be set by positionOnScreen() before first show.
}

// ── Mouse dragging ──────────────────────────────────────────────────────────

void MiniPlayer::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        dragStartPos_ = event->globalPosition().toPoint();
        dragged_ = false;

        if (draggable_) {
            dragging_ = true;
            setCursor(Qt::ClosedHandCursor);
        }
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void MiniPlayer::mouseMoveEvent(QMouseEvent *event)
{
    if (dragging_) {
        const QPoint delta = event->globalPosition().toPoint() - dragStartPos_;
        if (!dragged_ && (delta.manhattanLength() >= 5))
            dragged_ = true;
        move(pos() + delta);
        dragStartPos_ = event->globalPosition().toPoint();
        posMode_ = PosCustom;           // drag → custom positioning
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void MiniPlayer::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    const bool wasDragging = dragging_;
    dragging_ = false;

    if (draggable_)
        setCursor(Qt::OpenHandCursor);

    // If the window was actually dragged, update anchor and persist the new
    // position immediately so next positionOnScreen() doesn't reload the old
    // saved position from Config.
    if (dragged_) {
        anchorX_ = x();
        anchorY_ = y();
        anchorCorner_ = AnchorTopLeft;  // absolute position = top-left
        auto &cfg = Config::instance();
        cfg.setMiniPlayerX(anchorX_);
        cfg.setMiniPlayerY(anchorY_);
        event->accept();
        return;
    }

    // Hide-on-click: skip if disabled or if the click was on a button or cover.
    if (hideOnClick_) {
        QWidget *child = childAt(event->position().toPoint());
        if (child == prevBtn_ || child == playBtn_
            || child == stopBtn_ || child == nextBtn_
            || child == coverWidget_) {
            event->accept();
            return;
        }
        hide();
    }

    event->accept();
}

// ── Drag enable/disable ─────────────────────────────────────────────────────

void MiniPlayer::setDraggable(bool on)
{
    // If currently dragging while disabling, cancel.
    if (!on && dragging_) {
        dragging_ = false;
        setCursor(Qt::ArrowCursor);
    }
    draggable_ = on;
    setCursor(on ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

void MiniPlayer::setHideOnClick(bool on)
{
    hideOnClick_ = on;
}

void MiniPlayer::setOpacityPercent(int percent)
{
    opacityPercent_ = qBound(0, percent, 100);
    setWindowOpacity(opacityPercent_ / 100.0);
}

// ── Resize event — re‑anchor corner modes ───────────────────────────────────

void MiniPlayer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // For corner modes the pinned edge must stay fixed when the window
    // resizes (e.g. longer/shorter title changes width).
    // Apply the cached anchor — computed once per mode/screen change.
    if (posMode_ == PosTopLeft    || posMode_ == PosTopRight ||
        posMode_ == PosBottomLeft || posMode_ == PosBottomRight) {
        applyAnchorPosition();
    }
}

// ── Hide event — save position ──────────────────────────────────────────────

void MiniPlayer::hideEvent(QHideEvent *event)
{
    saveGeometry();
    QWidget::hideEvent(event);
}

// ── Auto-hide ───────────────────────────────────────────────────────────────

void MiniPlayer::startAutoHide()
{
    stopAutoHide();
    if (timeoutSec_ > 0)
        autoHideTimer_->start(timeoutSec_ * 1000);
}

void MiniPlayer::stopAutoHide()
{
    autoHideTimer_->stop();
}

// ── Style presets ─────────────────────────────────────────────────────────

void MiniPlayer::applyStyle()
{
    presetBg_ = presetFg_ = presetBtnBg_ = presetBtnFg_ = {};

    if (style_ == Dark) {
        presetBg_    = QStringLiteral("#2b2b2b");
        presetFg_    = QStringLiteral("#e0e0e0");
        presetBtnBg_ = QStringLiteral("#3c3c3c");
        presetBtnFg_ = QStringLiteral("#ffffff");
    } else {
        presetBg_    = QStringLiteral("#f0f0f0");
        presetFg_    = QStringLiteral("#222222");
        presetBtnBg_ = QStringLiteral("#e0e0e0");
        presetBtnFg_ = QStringLiteral("#222222");
    }

    // Popup background — stylesheet with border-radius, no paintEvent.
    setStyleSheet(QStringLiteral(
        "background: %1; border: 1px solid %2; border-radius: %3px;")
        .arg(presetBg_, presetFg_)
        .arg(kRadius));

    // Button-bar inherits NOTHING from parent stylesheet (background
    // is NOT inherited in Qt stylesheets) — make it transparent.
    buttonBar_->setAttribute(Qt::WA_TranslucentBackground, true);

    // Labels.
    const QString labelStyle = QStringLiteral(
        "color: %1; background: transparent; border: none; padding: 0;")
        .arg(presetFg_);
    titleLabel_->setStyleSheet(labelStyle + QStringLiteral(" font-weight: bold;"));
    artistLabel_->setStyleSheet(labelStyle);
    albumLabel_->setStyleSheet(labelStyle);

    // Transport buttons.
    const QString btnStyle = QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none;"
        "  border-radius: 3px; padding: 2px; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:pressed { background: %4; }")
        .arg(presetBtnBg_, presetBtnFg_,
             style_ == Dark ? "#555555" : "#cccccc",
             style_ == Dark ? "#777777" : "#aaaaaa");
    for (auto *b : {prevBtn_, playBtn_, stopBtn_, nextBtn_}) {
        if (b) b->setStyleSheet(btnStyle);
    }

    update();
}

// ── Setters ─────────────────────────────────────────────────────────────────

void MiniPlayer::setStylePreset(StylePreset preset)
{
    style_ = preset;
    applyStyle();
}

void MiniPlayer::setTimeoutSec(int sec)
{
    timeoutSec_ = qMax(0, sec);
}

void MiniPlayer::setShowCover(bool show)
{
    showCover_ = show;
    if (coverWidget_)
        coverWidget_->setVisible(show);
}

void MiniPlayer::setShowButtons(bool show)
{
    showButtons_ = show;
    if (buttonBar_)
        buttonBar_->setVisible(show);
}

// ── Position setters ────────────────────────────────────────────────────────

void MiniPlayer::setPositionMode(PositionMode m)
{
    posMode_ = m;
}

void MiniPlayer::setTargetScreen(int idx)
{
    screenIdx_ = idx;
}

void MiniPlayer::setMarginX(int px)
{
    marginX_ = px;
}

void MiniPlayer::setMarginY(int px)
{
    marginY_ = px;
}
