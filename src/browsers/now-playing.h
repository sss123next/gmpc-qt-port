#pragma once

/**
 * @file now-playing.h
 * @brief Full-page «Now Playing» screen — large cover art, song lyrics,
 *        and tabbed metadata panels.
 *
 * Отдельная страница, а не строка в control bar: здесь есть место под крупную
 * обложку, текст песни и вкладки с детальной информацией о трэке (теги MPD).
 */

#include <QString>
#include <QTabWidget>
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

    void onConnectionChanged(bool connected) override;

private slots:
    /** @brief Текст песни пришёл: показываем, только если он от текущего трэка. */
    void onLyricsReady(const QString &artist, const QString &title,
                       const QString &lyrics);
    /// Лирики нет ни у одного провайдера.
    void onLyricsNotFound(const QString &artist, const QString &title);

private:
    /** @brief Ключ «артист\\nназвание» текущего запроса: ответы по уже сменившемуся
     *  трэку отбрасываются — иначе текст чужой песни занял бы экран. */
    static QString lyricsKey(const QString &artist, const QString &title);
    /// Очищает всё под «ничего не играет».
    void showNothing();
    /// Дозапросить полные теги у MPD и обновить вкладки.
    void refreshMetadata();

    Connection      *conn_ = nullptr;
    MetadataManager *mgr_ = nullptr;
    CoverWidget *cover_ = nullptr;
    CoverViewer *coverViewer_ = nullptr;
    QLabel *titleLabel_;
    QLabel *artistLabel_;
    QLabel *albumLabel_;
    QPlainTextEdit *lyricsView_;
    QTabWidget *tabs_;
    /// Строка состояния страницы («ничего не играет», «лирика ищется…»).
    QLabel *hintLabel_;

    QString curArtist_;   ///< артист трэка, для которого ждём лирику
    QString curTitle_;    ///< название того же трэка
    QString curUri_;      ///< URI для дозапроса тегов
};
