#pragma once

/**
 * @file tray-popup.h
 * @brief Borderless mini-player window (detached from tray).
 *
 * Shows album cover, song title/artist/album, and optional transport
 * buttons in a frameless window with dark/light style presets.
 * Background drawn via plain stylesheet (border-radius for rounded
 * corners), cover via CoverWidget (proven to work in NowPlaying).
 */

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QPushButton>
#include <QStyleOption>

class MetadataManager;
class CoverWidget;
struct SongEntry;

/// Borderless mini‑player with cover, song info, and transport controls.
class TrayPopup : public QWidget {
    Q_OBJECT
public:
    /// Built-in colour presets.
    enum StylePreset {
        Dark  = 0,  ///< Dark background, light text.
        Light = 1,  ///< Light background, dark text.
    };
    Q_ENUM(StylePreset)

    explicit TrayPopup(QWidget *parent = nullptr);

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

signals:
    void playPauseClicked();
    void stopClicked();
    void nextClicked();
    void previousClicked();

private:
    void positionOnScreen();
    void startAutoHide();
    void stopAutoHide();
    void applyStyle();
    void createLayout();

    MetadataManager *mgr_ = nullptr;
    CoverWidget *coverWidget_ = nullptr;

    StylePreset style_ = Dark;
    int timeoutSec_ = 0;
    bool showCover_ = true;
    bool showButtons_ = true;

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
};
