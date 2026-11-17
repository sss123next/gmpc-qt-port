#pragma once

/**
 * @file tray-popup.h
 * @brief Borderless popup window near tray icon (like original gmpc).
 *
 * Shows album cover, song title/artist/album, and optional transport
 * buttons in a frameless window with dark/light style presets.
 * Auto-hides after a configurable timeout; mouse-over postpones hide.
 * Position is configurable relative to the tray icon.
 */

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QPushButton>
#include <QStyleOption>

class MetadataManager;
struct SongEntry;

/// Borderless popup with cover, song info, and transport controls.
class TrayPopup : public QWidget {
    Q_OBJECT
public:
    /// Position modes relative to the tray icon.
    enum Position {
        AtCursor        = 0,  ///< Show at current mouse cursor.
        NearTrayTopLeft     = 1,  ///< Top-left corner near icon.
        NearTrayTopRight    = 2,  ///< Top-right corner near icon.
        NearTrayBottomLeft  = 3,  ///< Bottom-left corner near icon.
        NearTrayBottomRight = 4,  ///< Bottom-right corner near icon.
    };
    Q_ENUM(Position)

    /// Built-in colour presets.
    enum StylePreset {
        Dark  = 0,  ///< Dark background, light text.
        Light = 1,  ///< Light background, dark text.
    };
    Q_ENUM(StylePreset)

    explicit TrayPopup(QWidget *parent = nullptr);

    /// Provide a MetadataManager for cover-art fetching.
    void setMetadataManager(MetadataManager *mgr);

    /** @brief Update the popup for the current song and show near @p iconPos.
     *  @param song   Current song (may be null → clears display).
     *  @param iconPos Screen position of the tray icon (for positioning). */
    void showSong(const SongEntry *song, const QPoint &iconPos);

    /// Immediately clear display and hide.
    void clear();

    // ── Configuration ────────────────────────────────────────────────
    void setPosition(Position pos);
    void setStylePreset(StylePreset preset);
    void setTimeoutSec(int sec);
    void setShowCover(bool show);
    void setShowButtons(bool show);
    void setOffsets(int x, int y);

    Position position() const      { return position_; }
    StylePreset stylePreset() const { return style_; }
    int timeoutSec() const         { return timeoutSec_; }
    bool showCover() const         { return showCover_; }
    bool showButtons() const       { return showButtons_; }
    QPair<int,int> offsets() const { return {xOffset_, yOffset_}; }

signals:
    void playPauseClicked();
    void stopClicked();
    void nextClicked();
    void previousClicked();

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void reposition(const QPoint &iconPos);
    void startAutoHide();
    void stopAutoHide();
    void applyStyle();
    void createLayout();
    void fetchCoverFor(const QString &artist, const QString &album);

    MetadataManager *mgr_ = nullptr;

    Position position_ = AtCursor;
    StylePreset style_ = Dark;
    int timeoutSec_ = 10;
    bool showCover_ = true;
    bool showButtons_ = true;
    int xOffset_ = 0;
    int yOffset_ = 0;

    // Widgets
    QWidget *container_ = nullptr;    ///< Rounded-rect background container.
    QLabel  *coverLabel_ = nullptr;   ///< Album art (80×80).
    QLabel  *titleLabel_ = nullptr;   ///< Track title.
    QLabel  *artistLabel_ = nullptr;  ///< Artist name.
    QLabel  *albumLabel_ = nullptr;   ///< Album name (+ year).
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
