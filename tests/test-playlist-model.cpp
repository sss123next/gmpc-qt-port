#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-playlist-model.cpp
 * @brief Tests for PlaylistModel (QAbstractTableModel for the MPD queue).
 *
 * Requires QApplication (offscreen) for Qt model/view machinery.
 */

#include <QApplication>
#include <QMimeData>
#include <QModelIndex>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QStringList>

#include "core/playlist-model.h"

static int argc = 1;
static char arg0[] = "gmpc-qt-test";
static char *argv = arg0;
static QApplication app(argc, &argv);

TEST_CASE("PlaylistModel initial state is empty")
{
    PlaylistModel model;
    CHECK_EQ(model.rowCount({}), 0);
}

TEST_CASE("PlaylistModel roles contain expected keys")
{
    PlaylistModel model;

    // Even with 0 rows we can check roleNames()
    auto roles = model.roleNames();
    CHECK_FALSE(roles.isEmpty());

    // Our Roles enum has Title = Qt::UserRole + 1, Artist, Album, etc.
    CHECK(roles.contains(PlaylistModel::TitleRole));
    CHECK(roles.contains(PlaylistModel::ArtistRole));
    CHECK(roles.contains(PlaylistModel::AlbumRole));
    CHECK(roles.contains(PlaylistModel::DurationRole));
}

TEST_CASE("PlaylistModel data() returns empty for out-of-range index")
{
    PlaylistModel model;
    QModelIndex idx = model.index(0, 0);
    CHECK_FALSE(idx.isValid());
    CHECK(model.data(idx, Qt::DisplayRole).isValid() == false);
}

TEST_CASE("PlaylistModel can be cleared safely before any data loaded")
{
    PlaylistModel model;
    // Calling beginResetModel / endResetModel internal — but we can
    // check that rowCount is 0 and clear doesn't crash.
    model.clear();
    CHECK_EQ(model.rowCount({}), 0);
}

TEST_CASE("PlaylistModel emits modelReset on clear")
{
    PlaylistModel model;
    bool resetEmitted = false;
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&] {
        resetEmitted = true;
    });
    model.clear();
    CHECK(resetEmitted);
}

TEST_CASE("headerData returns column name for known columns")
{
    PlaylistModel model;
    CHECK_FALSE(model.headerData(0, Qt::Horizontal).toString().isEmpty());
}

// ── Current song (source of truth is MPD status) ────────────────────────────

namespace {

SongEntry makeSong(int id, int pos, const QString &title)
{
    SongEntry e;
    e.id = id;
    e.pos = pos;
    e.artist = QStringLiteral("artist");
    e.title = title;
    e.album = QStringLiteral("album");
    e.duration = 100 + pos;
    return e;
}

QList<SongEntry> threeSongs()
{
    return { makeSong(101, 0, "first"),
             makeSong(102, 1, "second"),
             makeSong(103, 2, "third") };
}

/// Collects every id passed to currentSongChanged().
class SongSignals {
public:
    explicit SongSignals(PlaylistModel &model)
    {
        QObject::connect(&model, &PlaylistModel::currentSongChanged,
                         [this](int id) { ids.append(id); });
    }
    QList<int> ids;
};

} // namespace

TEST_CASE("no song is current until MPD reports one")
{
    PlaylistModel model;
    model.setSongs(threeSongs());
    // Regression: the model used to hand out songs_[0] as "current".
    CHECK(model.currentSong() == nullptr);
    CHECK_EQ(model.currentSongId(), -1);
}

TEST_CASE("the current song is the one status names, not row zero")
{
    PlaylistModel model;
    model.setSongs(threeSongs());
    model.setCurrentSong(102, 1);

    REQUIRE(model.currentSong() != nullptr);
    CHECK_EQ(model.currentSong()->id, 102);
    CHECK_EQ(model.currentSong()->title, QStringLiteral("second"));
}

TEST_CASE("the song id wins over a stale queue position")
{
    PlaylistModel model;
    model.setSongs(threeSongs());
    // MPD can report a position that no longer matches after queue edits; the
    // id is stable, so it must decide.
    model.setCurrentSong(103, 0);

    REQUIRE(model.currentSong() != nullptr);
    CHECK_EQ(model.currentSong()->id, 103);
}

TEST_CASE("the position resolves the song when no id is known")
{
    PlaylistModel model;
    model.setSongs(threeSongs());
    model.setCurrentSong(-1, 2);

    REQUIRE(model.currentSong() != nullptr);
    CHECK_EQ(model.currentSong()->title, QStringLiteral("third"));
}

TEST_CASE("an unknown position yields no current song")
{
    PlaylistModel model;
    model.setSongs(threeSongs());
    model.setCurrentSong(999, 42);
    CHECK(model.currentSong() == nullptr);
}

TEST_CASE("currentSongChanged fires once per resolved song")
{
    PlaylistModel model;
    SongSignals emitted(model);

    model.setSongs(threeSongs());
    model.setCurrentSong(102, 1);
    model.setCurrentSong(102, 1);      // identical status tick
    model.setSongs(threeSongs());      // reload of the same queue

    CHECK_EQ(emitted.ids, QList<int>{102});
}

TEST_CASE("loading the queue after status announces the current song")
{
    PlaylistModel model;
    SongSignals emitted(model);

    // Status arrives before the queue has been read: nothing to resolve yet.
    model.setCurrentSong(102, 1);
    CHECK_EQ(emitted.ids, QList<int>{-1});

    model.setSongs(threeSongs());
    REQUIRE(emitted.ids.size() == 2);
    CHECK_EQ(emitted.ids.last(), 102);
}

TEST_CASE("clearing the queue drops the current song")
{
    PlaylistModel model;
    model.setSongs(threeSongs());
    model.setCurrentSong(101, 0);
    REQUIRE(model.currentSong() != nullptr);

    SongSignals emitted(model);
    model.clear();

    CHECK_EQ(model.rowCount({}), 0);
    CHECK(model.currentSong() == nullptr);
    CHECK_EQ(emitted.ids, QList<int>{-1});
}

// ── Колонки (7.1: Title/Artist/Album/Time вместо единственной Path) ─────────

namespace {

/// Трек без тега Title, но с URI — как водится в нечищеных фонотеках.
SongEntry untitledSong(int id, int pos, const QString &uri)
{
    SongEntry e;
    e.id = id;
    e.pos = pos;
    e.uri = uri;
    e.duration = 0;
    return e;
}

QString cell(PlaylistModel &model, int row, int column)
{
    return model.data(model.index(row, column), Qt::DisplayRole).toString();
}

QList<int> idsInOrder(const PlaylistModel &model)
{
    QList<int> ids;
    for (int i = 0; i < model.rowCount({}); ++i)
        if (const SongEntry *s = model.songAt(i))
            ids.append(s->id);
    return ids;
}

QList<int> positionsInOrder(const PlaylistModel &model)
{
    QList<int> pos;
    for (int i = 0; i < model.rowCount({}); ++i)
        if (const SongEntry *s = model.songAt(i))
            pos.append(s->pos);
    return pos;
}

} // namespace

TEST_CASE("the queue has four named columns")
{
    PlaylistModel model;
    CHECK_EQ(model.columnCount({}), int(PlaylistModel::ColumnCount));

    QStringList headers;
    for (int c = 0; c < model.columnCount({}); ++c)
        headers << model.headerData(c, Qt::Horizontal).toString();
    CHECK_FALSE(headers.contains(QString()));
    CHECK_EQ(QSet<QString>(headers.begin(), headers.end()).size(), headers.size());
}

TEST_CASE("each column shows its own tag")
{
    PlaylistModel model;
    model.setSongs(threeSongs());   // artist «artist», album «album», duration 100+pos

    CHECK_EQ(cell(model, 0, PlaylistModel::TitleColumn), QStringLiteral("first"));
    CHECK_EQ(cell(model, 1, PlaylistModel::ArtistColumn), QStringLiteral("artist"));
    CHECK_EQ(cell(model, 2, PlaylistModel::AlbumColumn), QStringLiteral("album"));
    // 102 с = 1:42 — не «102» и не «1.42».
    CHECK_EQ(cell(model, 2, PlaylistModel::TimeColumn), QStringLiteral("1:42"));
}

TEST_CASE("a track without a Title falls back to its file name")
{
    PlaylistModel model;
    SongEntry withUri = untitledSong(201, 0, QStringLiteral("music/odyssey/09 - Interstella.mp3"));
    model.setSongs({withUri});

    CHECK_EQ(cell(model, 0, PlaylistModel::TitleColumn),
             QStringLiteral("09 - Interstella.mp3"));
}

TEST_CASE("an unknown duration is not shown as zero")
{
    PlaylistModel model;
    SongEntry noDuration = untitledSong(202, 0, QStringLiteral("stream:radio"));
    noDuration.duration = 0;   // потоку длительность неизвестна
    model.setSongs({noDuration});

    CHECK_EQ(cell(model, 0, PlaylistModel::TimeColumn), QStringLiteral("--:--"));
}

// ── Перемещение и удаление строк (7.1: DnD и контекстное меню) ──────────────

TEST_CASE("moving rows down lands them before the target row")
{
    PlaylistModel model;
    model.setSongs({makeSong(1, 0, "a"), makeSong(2, 1, "b"), makeSong(3, 2, "c"),
                    makeSong(4, 3, "d"), makeSong(5, 4, "e")});

    // a,b — перед d: получаем c,a,b,d,e.
    REQUIRE(model.moveRowsWithin(0, 2, 3));
    CHECK_EQ(idsInOrder(model), QList<int>{3, 1, 2, 4, 5});
}

TEST_CASE("moving rows up lands them before the target row")
{
    PlaylistModel model;
    model.setSongs({makeSong(1, 0, "a"), makeSong(2, 1, "b"), makeSong(3, 2, "c"),
                    makeSong(4, 3, "d"), makeSong(5, 4, "e")});

    // d,e — перед b: получаем a,d,e,b,c.
    REQUIRE(model.moveRowsWithin(3, 2, 1));
    CHECK_EQ(idsInOrder(model), QList<int>{1, 4, 5, 2, 3});
}

TEST_CASE("moving to the end appends and minus one means the same")
{
    PlaylistModel model;
    model.setSongs({makeSong(1, 0, "a"), makeSong(2, 1, "b"), makeSong(3, 2, "c")});

    REQUIRE(model.moveRowsWithin(0, 1, -1));
    CHECK_EQ(idsInOrder(model), QList<int>{2, 3, 1});

    model.setSongs({makeSong(1, 0, "a"), makeSong(2, 1, "b"), makeSong(3, 2, "c")});
    REQUIRE(model.moveRowsWithin(0, 1, model.rowCount({})));
    CHECK_EQ(idsInOrder(model), QList<int>{2, 3, 1});
}

TEST_CASE("positions follow the row number after a move")
{
    PlaylistModel model;
    model.setSongs(threeSongs());   // pos уже 0..2
    REQUIRE(model.moveRowsWithin(2, 1, 0));

    CHECK_EQ(positionsInOrder(model), QList<int>{0, 1, 2});
    const SongEntry *moved = model.songAt(0);
    REQUIRE(moved != nullptr);
    CHECK_EQ(moved->id, 103);
    CHECK_EQ(moved->pos, 0);
}

TEST_CASE("a move that changes nothing is refused")
{
    PlaylistModel model;
    model.setSongs(threeSongs());

    CHECK_FALSE(model.moveRowsWithin(1, 1, 1));      // на себя
    CHECK_FALSE(model.moveRowsWithin(1, 2, 3));      // внутрь своего диапазона
    CHECK_FALSE(model.moveRowsWithin(0, 5, 2));      // за пределы модели
    CHECK_FALSE(model.moveRowsWithin(-1, 1, 2));
    CHECK_FALSE(model.moveRowsWithin(0, 0, 2));
    CHECK_EQ(idsInOrder(model), QList<int>{101, 102, 103});
}

TEST_CASE("removing several rows keeps the rest in order")
{
    PlaylistModel model;
    model.setSongs({makeSong(1, 0, "a"), makeSong(2, 1, "b"), makeSong(3, 2, "c"),
                    makeSong(4, 3, "d"), makeSong(5, 4, "e")});

    // Непорядок, повтор и выход за пределы — частая комбинация: выделение не
    // отсортировано, а индексы устаревших строк в нём вполне возможны. Удаляются
    // строки 0 (id 1) и 3 (id 4).
    model.removeRowsAt({3, 0, 3, 99, -2});
    CHECK_EQ(idsInOrder(model), QList<int>{2, 3, 5});
    CHECK_EQ(positionsInOrder(model), QList<int>{0, 1, 2});
}

TEST_CASE("removing the playing row announces that nothing plays")
{
    PlaylistModel model;
    model.setSongs(threeSongs());
    model.setCurrentSong(102, 1);
    REQUIRE(model.currentSong() != nullptr);

    SongSignals emitted(model);
    model.removeRowsAt({1});

    CHECK_EQ(idsInOrder(model), QList<int>{101, 103});
    CHECK_EQ(emitted.ids, QList<int>{-1});
}

TEST_CASE("removing another row leaves the current song alone")
{
    PlaylistModel model;
    model.setSongs(threeSongs());
    model.setCurrentSong(102, 1);

    SongSignals emitted(model);
    model.removeRowsAt({2});   // играющий трек не затронут, позиции сдвинулись ниже

    CHECK_EQ(idsInOrder(model), QList<int>{101, 102});
    REQUIRE(model.currentSong() != nullptr);
    CHECK_EQ(model.currentSong()->id, 102);
    CHECK(emitted.ids.isEmpty());
}

// ── Координаты перемещения (экран ↔ аргумент TO команды move_range) ─────────

namespace {

QList<int> fiveIds() { return {1, 2, 3, 4, 5}; }

} // namespace

TEST_CASE("a downward move targets the position after the block is lifted")
{
    // a,b,c,d,e: блок a,b перед d (destRow=3). MPD считает TO по очереди без
    // переносимых строк, поэтому итоговая позиция — 1, а не 3.
    CHECK_EQ(PlaylistModel::targetPosForMove(0, 2, 3, 5), 1);
    // Вверх считанные строки не мешают: d,e перед b → позиция 1.
    CHECK_EQ(PlaylistModel::targetPosForMove(3, 2, 1, 5), 1);
}

TEST_CASE("the end of the queue is a position inside it")
{
    // -1 и «перед строкой за концом» означают одно: встать последним.
    CHECK_EQ(PlaylistModel::targetPosForMove(0, 1, -1, 3), 2);
    CHECK_EQ(PlaylistModel::targetPosForMove(0, 1, 3, 3), 2);
    CHECK_EQ(PlaylistModel::targetPosForMove(0, 1, 99, 3), 2);   // ограничиваем хвостом
}

TEST_CASE("geometry outside the queue is refused")
{
    CHECK_EQ(PlaylistModel::targetPosForMove(1, 1, 1, 5), -1);   // на себя
    CHECK_EQ(PlaylistModel::targetPosForMove(1, 2, 3, 5), -1);   // внутрь блока
    CHECK_EQ(PlaylistModel::targetPosForMove(4, 2, 0, 5), -1);   // за конец очереди
    CHECK_EQ(PlaylistModel::targetPosForMove(-1, 1, 0, 5), -1);
    CHECK_EQ(PlaylistModel::targetPosForMove(0, 0, 2, 5), -1);
    CHECK_EQ(PlaylistModel::targetPosForMove(0, 1, 2, 0), -1);   // пустая очередь
}

TEST_CASE("the command coordinate and the local move agree")
{
    // Для любой допустимой перестановки порядок, который даёт локальная копия,
    // должен совпадать с порядком MPD после move_range <from>-<to> в TO. Иначе
    // очередь на экране разъедется с очередью сервера при первом же plchanges.
    const int size = 5;
    for (int count = 1; count <= 3; ++count) {
        for (int sourceRow = 0; sourceRow + count <= size; ++sourceRow) {
            for (int destRow = -1; destRow <= size; ++destRow) {
                const int toPos = PlaylistModel::targetPosForMove(sourceRow, count,
                                                                  destRow, size);
                INFO("sourceRow=" << sourceRow << " count=" << count
                                   << " destRow=" << destRow << " toPos=" << toPos);

                PlaylistModel model;
                QList<SongEntry> songs;
                for (int i = 0; i < size; ++i)
                    songs << makeSong(i + 1, i, QStringLiteral("t%1").arg(i + 1));
                model.setSongs(songs);

                if (toPos < 0) {
                    CHECK_FALSE(model.moveRowsWithin(sourceRow, count, destRow));
                    continue;
                }
                REQUIRE(model.moveRowsWithin(sourceRow, count, destRow));

                // Так же вставляет MPD: вынуть блок, затем вставить по TO.
                QList<int> rest = fiveIds();
                const QList<int> block = rest.mid(sourceRow, count);
                for (int i = 0; i < count; ++i)
                    rest.removeAt(sourceRow);
                for (int i = 0; i < count; ++i)
                    rest.insert(toPos + i, block[i]);

                CHECK_EQ(idsInOrder(model), rest);
            }
        }
    }
}

TEST_CASE("sorting by duration compares seconds, not text")
{
    PlaylistModel model;
    SongEntry longTrack = makeSong(1, 0, "long");
    longTrack.duration = 600;                       // 10:00
    SongEntry shortTrack = makeSong(2, 1, "short");
    shortTrack.duration = 9;                        // 0:09
    model.setSongs({longTrack, shortTrack});

    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    proxy.setSortRole(PlaylistModel::SortRole);
    proxy.sort(PlaylistModel::TimeColumn, Qt::AscendingOrder);

    // По DisplayRole «0:09» проиграл бы «10:00»; по SortRole порядок числовой.
    REQUIRE_EQ(proxy.rowCount({}), 2);
    CHECK_EQ(model.songAt(proxy.mapToSource(proxy.index(0, 0)).row())->id, 2);
    CHECK_EQ(model.songAt(proxy.mapToSource(proxy.index(1, 0)).row())->id, 1);

    // Текстовые колонки сортируются тем же текстом, что показан на экране.
    CHECK_EQ(model.data(model.index(0, PlaylistModel::TitleColumn),
                        PlaylistModel::SortRole).toString(),
             cell(model, 0, PlaylistModel::TitleColumn));
}

// ── Перетаскивание строк (mimeData ↔ songIdsFromMimeData) ───────────────────

TEST_CASE("dragged rows carry their song ids")
{
    PlaylistModel model;
    model.setSongs(threeSongs());

    QModelIndexList picked;
    for (int col = 0; col < model.columnCount({}); ++col)   // выделение строки — это все колонки
        picked << model.index(1, col);
    picked << model.index(0, PlaylistModel::TitleColumn);

    QMimeData *data = model.mimeData(picked);
    REQUIRE(data != nullptr);
    CHECK_EQ(PlaylistModel::songIdsFromMimeData(data), QList<int>{102, 101});
    // Обычный текст — чтобы перетаскивание наружу не приносило пустоту.
    CHECK_FALSE(data->text().isEmpty());
    delete data;
}

TEST_CASE("data from anywhere else is not ours")
{
    QMimeData foreign;
    foreign.setText(QStringLiteral("some text"));
    CHECK(PlaylistModel::songIdsFromMimeData(&foreign).isEmpty());
    CHECK(PlaylistModel::songIdsFromMimeData(nullptr).isEmpty());

    PlaylistModel model;
    model.setSongs(threeSongs());
    // Строка без идентификатора в перетаскивание не попадает.
    SongEntry noId = makeSong(-1, 0, "no id");
    model.setSongs({noId});
    QMimeData *data = model.mimeData({model.index(0, PlaylistModel::TitleColumn)});
    REQUIRE(data != nullptr);
    CHECK(PlaylistModel::songIdsFromMimeData(data).isEmpty());
    delete data;
}

TEST_CASE("rows survive a move and a removal in the same session")
{
    // Последовательность как у пользователя: перенёс, удалил — идентификаторы и
    // позиции остаются согласованными после обеих операций.
    PlaylistModel model;
    QList<SongEntry> songs;
    for (int i = 0; i < fiveIds().size(); ++i)
        songs << makeSong(fiveIds()[i], i, QStringLiteral("t%1").arg(i + 1));
    model.setSongs(songs);

    REQUIRE(model.moveRowsWithin(4, 1, 0));          // e в начало
    CHECK_EQ(idsInOrder(model), QList<int>{5, 1, 2, 3, 4});
    model.removeRowsAt({2});                          // b
    CHECK_EQ(idsInOrder(model), QList<int>{5, 1, 3, 4});
    CHECK_EQ(positionsInOrder(model), QList<int>{0, 1, 2, 3});
    CHECK_EQ(model.posForId(3), 2);
}
