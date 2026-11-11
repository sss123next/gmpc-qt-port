#pragma once

/**
 * @file song-info-dialog.h
 * @brief Окно «Song Info» — метаданные выбранного трэка и текст песни.
 *
 * Открывается пунктом «Info» контекстного меню браузеров. Теги приходят готовым
 * SongEntry (их достаёт MainWindow у сервера по URI строки), лирику тянет сам диалог
 * через MetadataManager — это его потребитель; без менеджера окно живёт на одних тегах.
 */

#include <QDialog>

#include "core/playlist-model.h"

class QLabel;
class QPlainTextEdit;
class MetadataManager;

/// Теги одного трэка + текст песни (потребитель LyricsProvider).
class SongInfoDialog : public QDialog {
    Q_OBJECT
public:
    /** @brief Показывает метаданные трэка и запрашивает его лирику.
     *  @param song   теги строки, выбранной в браузере
     *  @param mgr    менеджер метаданных; nullptr — только теги, без сети
     */
    explicit SongInfoDialog(const SongEntry &song,
                            MetadataManager *mgr = nullptr,
                            QWidget *parent = nullptr);

private slots:
    /** @brief Текст песни пришёл: показываем, если он от этого трэка. */
    void onLyricsReady(const QString &artist, const QString &title,
                       const QString &lyrics);
    /// Лирики нет ни у одного провайдера.
    void onLyricsNotFound(const QString &artist, const QString &title);

private:
    /** @brief Ключ «артист\\nназвание»: ответы по чужому трэку отбрасываются —
     *  окно могло остаться открытым с прошлого выбора. */
    static QString lyricsKey(const QString &artist, const QString &title);

    MetadataManager *mgr_ = nullptr;
    QLabel *hintLabel_;
    QPlainTextEdit *lyricsView_;

    QString curArtist_;   ///< артист трэка, для которого ждём лирику
    QString curTitle_;    ///< название того же трэка
};