#pragma once

/**
 * @file mini-player.h
 * @brief Borderless mini-player window with cover, song info, and transport buttons.
 *
 * A frameless, always-on-top widget showing album art, current song metadata,
 * and optional playback controls.  Position can be saved and restored across
 * sessions — the user drags it anywhere on screen.
 */

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QPushButton>
#include <QStyleOption>

class MetadataManager;
class CoverWidget;
class CoverViewer;
struct SongEntry;

/// Borderless mini‑player with cover, song info, and transport controls.
class MiniPlayer : public QWidget {
    Q_OBJECT
public:
    /// Built-in colour presets.
    enum StylePreset {
        Dark  = 0,  ///< Dark background, light text.
        Light = 1,  ///< Light background, dark text.
    };
    Q_ENUM(StylePreset)

    /// Screen-corner presets for positioning (matches Config values).
    enum PositionMode {
        PosCenter     = 0,  ///< Centre of the target screen.
        PosTopLeft    = 1,  ///< Top‑left corner + margins.
        PosTopRight   = 2,  ///< Top‑right corner + margins.
        PosBottomLeft = 3,  ///< Bottom‑left corner + margins.
        PosBottomRight= 4,  ///< Bottom‑right corner + margins.
        PosCustom     = 5,  ///< Dragged / saved absolute position.
    };
    Q_ENUM(PositionMode)

    explicit MiniPlayer(QWidget *parent = nullptr);

    /// Provide a MetadataManager for cover-art fetching (forwards to CoverWidget).
    void setMetadataManager(MetadataManager *mgr);

    /** @brief Update the window for the current song and show. */
    void showSong(const SongEntry *song);

    /// Immediately clear display and hide.
    void clear();

    // ── Configuration ────────────────────────────────────────────────
    void setStylePreset(StylePreset preset);
    void setTimeoutSec(int sec);
    void setShowCover(bool show);
    void setShowButtons(bool show);

    StylePreset stylePreset() const      { return style_; }
    int timeoutSec() const               { return timeoutSec_; }
    bool showCover() const               { return showCover_; }
    bool showButtons() const             { return showButtons_; }

    /// Saved window geometry (load before first show, save on hide).
    void saveGeometry();
    void restoreGeometry();

    /// Current position mode (from Config or set by drag).
    PositionMode positionMode() const { return posMode_; }
    void setPositionMode(PositionMode m);
    /// Screen index for positioning (-1 = primary).
    int targetScreen() const { return screenIdx_; }
    void setTargetScreen(int idx);
    /// Corner margin in px.
    int marginX() const { return marginX_; }
    void setMarginX(int px);
    int marginY() const { return marginY_; }
    void setMarginY(int px);
    /// Enable/disable mouse-drag repositioning (default off).
    void setDraggable(bool on);
    bool isDraggable() const { return draggable_; }

signals:
    void playPauseClicked();
    void stopClicked();
    void nextClicked();
    void previousClicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void positionOnScreen();
    void startAutoHide();
    void stopAutoHide();
    void applyStyle();
    void createLayout();
    /// Resolve the target QScreen from config (primary fallback).
    const QScreen *resolveScreen() const;

    MetadataManager *mgr_ = nullptr;
    CoverWidget *coverWidget_ = nullptr;
    CoverViewer *coverViewer_ = nullptr;

    StylePreset style_ = Dark;
    int timeoutSec_ = 0;
    bool showCover_ = true;
    bool showButtons_ = true;

    // Positioning
    PositionMode posMode_ = PosCenter;
    int screenIdx_ = -1;       ///< Screen index (-1 = primary).
    int marginX_ = 10;         ///< Corner margin x (px).
    int marginY_ = 10;         ///< Corner margin y (px).

    // Widgets
    QLabel  *titleLabel_ = nullptr;
    QLabel  *artistLabel_ = nullptr;
    QLabel  *albumLabel_ = nullptr;
    QWidget *buttonBar_ = nullptr;
    QPushButton *playBtn_ = nullptr;
    QPushButton *stopBtn_ = nullptr;
    QPushButton *nextBtn_ = nullptr;
    QPushButton *prevBtn_ = nullptr;

    QTimer *autoHideTimer_ = nullptr;

    // Cached preset colours
    QString presetBg_;
    QString presetFg_;
    QString presetBtnBg_;
    QString presetBtnFg_;

    // Drag state
    bool draggable_ = false;  ///< Whether mouse-drag is enabled.
    QPoint dragStartPos_;
    bool dragging_ = false;
};
