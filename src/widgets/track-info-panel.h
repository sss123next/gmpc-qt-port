#pragma once

/**
 * @file track-info-panel.h
 * @brief Переиспользуемая панель информации о трэке: обложка, теги, табы
 *        метаданных и текст песни.
 *
 * Используется и в Now Playing (текущий трэк), и в отдельном окне
 * для произвольного трэка. Единственное различие — источник данных:
 * Now Playing берёт текущий трэк из очереди, окно — конкретный URI.
 */

#include <QWidget>
#include <QLabel>
#include "core/playlist-model.h"

class Connection;
class MetadataManager;
class CoverWidget;
class CoverViewer;
class QTabWidget;
class QPlainTextEdit;
class QPushButton;
class QFormLayout;

/// Содержимое панели Now Playing, вынесенное в переиспользуемый виджет.
class TrackInfoPanel : public QWidget {
    Q_OBJECT
public:
    explicit TrackInfoPanel(QWidget *parent = nullptr);
    ~TrackInfoPanel() override;

    void setConnection(Connection *conn);
    void setMetadataManager(MetadataManager *mgr);

    /// Заполнить панель данными трэка. Пустой song — полная очистка.
    void showTrack(const SongEntry &song);
    /// Очистить всё до состояния «ничего не выбрано».
    void clear();

signals:
    /// Клик по обложке — для полноэкранного просмотра.
    void coverClicked();

private slots:
    void onLyricsReady(const QString &artist, const QString &title,
                       const QString &lyrics);
    void onLyricsNotFound(const QString &artist, const QString &title);
    void onRefreshCover();
    void onRefreshLyrics();

private:
    void buildTabs();
    void clearTabs();
    /** @brief Запросить у MPD полные теги трэка и обновить вкладки. */
    void fetchMetadata(const QString &uri);
    /** @brief Запросить лирику для текущего трэка. */
    void requestLyricsForCurrent();

    static QString lyricsKey(const QString &artist, const QString &title);
    static void setFormField(QLabel *label, const QString &text);
    static QLabel *addFormField(QFormLayout *form, const QString &label);

    Connection      *conn_ = nullptr;
    MetadataManager *mgr_  = nullptr;
    CoverWidget   *cover_  = nullptr;
    CoverViewer   *coverViewer_ = nullptr;

    QLabel *artistLabel_;
    QLabel *titleLabel_;
    QLabel *albumLabel_;
    QTabWidget *tabs_;
    QPlainTextEdit *lyricsView_;
    QLabel *hintLabel_;
    QPushButton *refreshCoverBtn_   = nullptr;
    QPushButton *refreshLyricsBtn_  = nullptr;

    // ── Вкладки метаданных ────────────────────────────────────────────────
    bool tabsBuilt_ = false;
    QWidget *detailsTab_ = nullptr;   QLabel *detTrack_ = nullptr, *detDisc_ = nullptr;
    QLabel *detYear_ = nullptr, *detOrigDate_ = nullptr, *detGenre_ = nullptr;
    QLabel *detAlbumArtist_ = nullptr, *detDuration_ = nullptr;

    QWidget *creditsTab_ = nullptr;   QLabel *credComposer_ = nullptr, *credPerformer_ = nullptr;
    QLabel *credConductor_ = nullptr, *credEnsemble_ = nullptr, *credGrouping_ = nullptr;
    QLabel *credMood_ = nullptr, *credLocation_ = nullptr, *credLabel_ = nullptr;
    QLabel *credWork_ = nullptr, *credMovement_ = nullptr, *credMovNum_ = nullptr;

    QWidget *idsTab_ = nullptr;       QLabel *idTitleSort_ = nullptr, *idArtistSort_ = nullptr;
    QLabel *idAlbumSort_ = nullptr, *idAlbumArtSort_ = nullptr, *idCompSort_ = nullptr;
    QLabel *idComment_ = nullptr;
    QLabel *idMbArtist_ = nullptr, *idMbAlbum_ = nullptr, *idMbAlbumArt_ = nullptr;
    QLabel *idMbTrack_ = nullptr, *idMbWork_ = nullptr, *idMbRelTrack_ = nullptr;
    QLabel *idMbRelGroup_ = nullptr;

    QWidget *techTab_ = nullptr;      QLabel *techSampleRate_ = nullptr, *techBitDepth_ = nullptr;
    QLabel *techChannels_ = nullptr, *techBitrate_ = nullptr, *techFormat_ = nullptr;
    QLabel *techFile_ = nullptr;

    QString curArtist_;
    QString curTitle_;
    QString curAlbum_;
    QString curUri_;
};
