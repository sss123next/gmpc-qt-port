#pragma once

/**
 * @file tray-popup.h
 * @brief Borderless mini-player window (detached from tray).
 *
 * Shows album cover, song title/artist/album, and optional transport
 * buttons in a frameless window with dark/light style presets.
 */

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QPushButton>
#include <QStyleOption>

class MetadataManager;
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

    /// Provide a MetadataManager for cover-art fetching.
    void setMetadataManager(MetadataManager *mgr);

    /** @brief Update the window for the current song and show.
     *  @param song   Current song (may be null → clears display). */
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

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void positionOnScreen();
    void startAutoHide();
    void stopAutoHide();
    void applyStyle();
    void createLayout();

    MetadataManager *mgr_ = nullptr;

    StylePreset style_ = Dark;
    int timeoutSec_ = 0;          ///< 0 = no auto-hide.
    bool showCover_ = true;
    bool showButtons_ = true;

    // Widgets
    QWidget *container_ = nullptr;    ///< Rounded-rect background container.
    QLabel  *coverLabel_ = nullptr;   ///< Album art (80×80).
    QLabel  *titleLabel_ = nullptr;   ///< Track title.
    QLabel  *artistLabel_ = nullptr;  ///< Artist name.
    QLabel  *albumLabel_ = nullptr;   ///< Album name.
    QWidget *buttonBar_ = nullptr;    ///< Transport buttons row.
    QPushButton *playBtn_ = nullptr;
    QPushButton *stopBtn_ = nullptr;
    QPushButton *nextBtn_ = nullptr;
    QPushButton *prevBtn_ = nullptr;

    QTimer *autoHideTimer_ = nullptr;

    // Cover-fetch tracking
    QString lastArtist_;
    QString lastAlbum_;
    quint64 coverFetchId_ = 0;

    // Cached preset colours (used by paintEvent instead of palette(), which
    // doesn't reflect stylesheets).
    QString presetBg_;
    QString presetFg_;
    QString presetBtnBg_;
    QString presetBtnFg_;
    QString presetCoverBg_;
};
