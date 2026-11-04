#pragma once

/**
 * @file database-browser.h
 * @brief MPD music database browser (tag-based navigation).
 *
 * Shows the MPD media library organised by artist/album in a QTreeView.
 * Supports switching the top-level tag group via QComboBox.
 */

#include "browser-widget.h"
#include "music-database-model.h"

class Connection;
class MusicDatabaseModel;
class QComboBox;
class QTreeView;

/// Browse the MPD music database by tag.
class DatabaseBrowser : public BrowserWidget {
    Q_OBJECT
public:
    explicit DatabaseBrowser(Connection *conn, QWidget *parent = nullptr);
    ~DatabaseBrowser() override;

    QString title() const override { return tr("Database"); }
    QString iconName() const override { return QStringLiteral("gmpc-database"); }

    /// The embedded tree view (for context menu setup).
    QTreeView *treeView() const { return view_; }

    /** @brief Что делать при активации строки дерева (двойной клик).
     *  Трэку отдаётся URI, группе и альбому — ограничения `findadd`.
     */
    MusicDbActivation activationFor(const QModelIndex &index) const {
        return model_->activationFor(index);
    }

    void onActivate() override;
    void onConnectionChanged(bool connected) override;

private slots:
    void onTagChanged(int index);

private:
    Connection *conn_;
    MusicDatabaseModel *model_;
    QTreeView *view_;
    QComboBox *tagCombo_;
    enum mpd_tag_type currentTag_ = MPD_TAG_ARTIST; ///< выбранный способ группировки
};

