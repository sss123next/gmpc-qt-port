/**
 * @file tray-popup.cpp
 * @brief Borderless mini-player window — simple stylesheet-based background,
 *        CoverWidget for proven cover-art display.
 */

#include "tray-popup.h"
#include "widgets/cover-widget.h"
#include "metadata/metadata-manager.h"
#include "core/playlist-model.h"

#include <QApplication>
#include <QScreen>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStyle>
#include <QDebug>

// ── Geometry constants ──────────────────────────────────────────────────────

static constexpr int kCoverPx     = 80;   ///< Cover image side (px).
static constexpr int kRadius      = 8;    ///< Corner radius for the popup.
static constexpr int kMargin      = 10;   ///< Internal margin.

// ── Constructor ─────────────────────────────────────────────────────────────

TrayPopup::TrayPopup(QWidget *parent)
    : QWidget(parent)
{
    // Frameless window, always on top, no taskbar entry.
    // Deliberately NO Qt::ToolTip — that flag lets the WM treat it as
    // a transient popup and override our positioning (e.g. near tray).
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint);
    // NO WA_TranslucentBackground — stylesheet handles background with
    // border-radius, no paintEvent needed.
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
    titleLabel_->setSizePolicy(QSizePolicy::MinimumExpanding,
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

    connect(prevBtn_, &QPushButton::clicked, this, &TrayPopup::previousClicked);
    connect(playBtn_, &QPushButton::clicked, this, &TrayPopup::playPauseClicked);
    connect(stopBtn_, &QPushButton::clicked, this, &TrayPopup::stopClicked);
    connect(nextBtn_, &QPushButton::clicked, this, &TrayPopup::nextClicked);

    right->addWidget(buttonBar_);
    buttonBar_->setVisible(showButtons_);

    root->addLayout(right, 1);
}

// ── Metadata Manager ────────────────────────────────────────────────────────

void TrayPopup::setMetadataManager(MetadataManager *mgr)
{
    mgr_ = mgr;
    if (coverWidget_)
        coverWidget_->setMetadataManager(mgr);
}

// ── Show / Update ───────────────────────────────────────────────────────────

void TrayPopup::showSong(const SongEntry *song)
{
    if (!song) {
        clear();
        return;
    }

    titleLabel_->setText(song->title.isEmpty() ? song->uri : song->title);
    artistLabel_->setText(song->artist);
    albumLabel_->setText(song->album);

    // Delegate cover fetching to CoverWidget (proven pipeline).
    if (!song->artist.isEmpty() && !song->album.isEmpty())
        coverWidget_->fetchCover(song->artist, song->album, song->uri);
    else
        coverWidget_->clear();

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
    coverWidget_->clear();
}

// ── Positioning ─────────────────────────────────────────────────────────────

void TrayPopup::positionOnScreen()
{
    const QScreen *screen = QApplication::primaryScreen();
    if (!screen) { move(100, 100); return; }
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

// ── Style presets ─────────────────────────────────────────────────────────

void TrayPopup::applyStyle()
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
    if (coverWidget_)
        coverWidget_->setVisible(show);
    adjustSize();
}

void TrayPopup::setShowButtons(bool show)
{
    showButtons_ = show;
    if (buttonBar_)
        buttonBar_->setVisible(show);
    adjustSize();
}
