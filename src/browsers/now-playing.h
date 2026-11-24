#pragma once

/**
 * @file now-playing.h
 * @brief Full-page «Now Playing» screen — large cover art, song lyrics,
 *        and tabbed metadata panels.
 *
 * Отдельная страница, а не строка в control bar: здесь есть место под крупную
 * обложку, текст песни и вкладки с детальной информацией о трэке (теги MPD).
 */

#include <QFormLayout>
#include <QString>
#include <QTabWidget>
#include <QPushButton>
#include "browser-widget.h"

class QLabel;
class QPlainTextEdit;
class QTabWidget;
class CoverWidget;
class CoverViewer;
class MetadataManager;
class Connection;
struct SongEntry;

/// Крупная обложка, метаданные текущего трэка, лирика и вкладки с тегами.
class NowPlaying : public BrowserWidget {
    Q_OBJECT
public:
    explicit NowPlaying(QWidget *parent = nullptr);

    /// Connection для дозапроса полных тегов MPD.
    void setConnection(Connection *conn);

    /// Менеджер для загрузки лирики; без него текст песни не запрашивается.
    void setMetadataManager(MetadataManager *mgr);

    QString title() const override { return tr("Now Playing"); }
    QString iconName() const override { return QStringLiteral("audio-x-generic"); }

    /// Показывает трэк (nullptr — «ничего не играет») и запрашивает его лирику.
    void setSong(const SongEntry *song);

    void onActivate() override;
    void onDeactivate() override;
    void onConnectionChanged(bool connected) override;

private slots:
    /** @brief Текст песни пришёл: показываем, только если он от текущего трэка. */
    void onLyricsReady(const QString &artist, const QString &title,
                       const QString &lyrics);
    /// Лирики нет ни у одного провайдера.
    void onLyricsNotFound(const QString &artist, const QString &title);

    /** @brief Принудительно перезапросить обложку (сброс кеша + рефетч). */
    void onRefreshCover();
    /** @brief Принудительно перезапросить текст песни (сброс кеша + рефетч). */
    void onRefreshLyrics();

private:
    /** @brief Ключ «артист\\nназвание» текущего запроса: ответы по уже сменившемуся
     *  трэку отбрасываются — иначе текст чужой песни занял бы экран. */
    static QString lyricsKey(const QString &artist, const QString &title);
    /// Очищает всё под «ничего не играет».
    void showNothing();
    /// Дозапросить полные теги у MPD и обновить вкладки.
    void refreshMetadata();

    /// Построить вкладки метаданных один раз (лениво, при первом refreshMetadata).
    void buildTabs();
    /// Очистить текст и скрыть все строки вкладок метаданных.
    void clearTabs();

    /** @brief Обновить текст и видимость строки в форме.
     *  Если @p text пуст — строка прячется. */
    static void setFormField(QLabel *label, const QString &text);
    /** @brief Создать строку «подпись: значение» и вернуть QLabel значения. */
    static QLabel *addFormField(QFormLayout *form, const QString &label);

    Connection      *conn_ = nullptr;
    MetadataManager *mgr_ = nullptr;
    CoverWidget *cover_ = nullptr;
    CoverViewer *coverViewer_ = nullptr;
    QLabel *titleLabel_;
    QLabel *artistLabel_;
    QLabel *albumLabel_;
    QPlainTextEdit *lyricsView_;
    QTabWidget *tabs_;
    QLabel *hintLabel_;
    QPushButton *refreshCoverBtn_ = nullptr;
    QPushButton *refreshLyricsBtn_ = nullptr;

    // ── Вкладки метаданных (создаются один раз, текст обновляется на месте) ──
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
    int lastSongId_ = -2;     ///< songid последнего показанного трэка (-2 = никогда)
    bool needCover_ = false;  ///< обложка не загружена (трэк сменился, а страница была скрыта)

    void requestLyricsForCurrent();
};
