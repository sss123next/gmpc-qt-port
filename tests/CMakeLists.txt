# ── Test suite for gmpc-qt ────────────────────────────────────────────────────
# Uses doctest (header-only) + trompeloeil (header-only mocks).
# Each test is a separate executable (no shared main).

find_package(doctest REQUIRED)
find_package(trompeloeil REQUIRED)

# ── Helper: add a single test ─────────────────────────────────────────────────
function(add_qt_test name target_lib)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
        ${target_lib}
    )
    doctest_register_test(${name})
endfunction()

# ── Core library (alias: collect all sources into an OBJECT lib) ──────────────
# We reuse the existing sources but compile them into a convenience library
# so tests can link against them without building the main gmpc-qt binary.

add_library(testcore OBJECT
    ${CMAKE_SOURCE_DIR}/src/core/config.cpp
    ${CMAKE_SOURCE_DIR}/src/core/connection.cpp
    ${CMAKE_SOURCE_DIR}/src/core/mpd-error.cpp
    ${CMAKE_SOURCE_DIR}/src/core/mpd-endpoint.cpp
    ${CMAKE_SOURCE_DIR}/src/core/mpd-commands.cpp
    ${CMAKE_SOURCE_DIR}/src/core/idle-watcher.cpp
    ${CMAKE_SOURCE_DIR}/src/core/mpd-env.cpp
    ${CMAKE_SOURCE_DIR}/src/core/search-engine.cpp
    ${CMAKE_SOURCE_DIR}/src/core/playlist-model.cpp
    ${CMAKE_SOURCE_DIR}/src/core/queue-diff.cpp
    ${CMAKE_SOURCE_DIR}/src/core/queue-sync.cpp
    ${CMAKE_SOURCE_DIR}/src/browsers/music-database-model.cpp
    ${CMAKE_SOURCE_DIR}/src/browsers/file-system-model.cpp
    # Общий слой намерений нужен и браузерным тестам, и меню очереди: он линкуется с
    # testcore, а не копируется в каждый тестовый исполняемый файл.
    ${CMAKE_SOURCE_DIR}/src/browsers/browser-intent.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/log.cpp
    ${CMAKE_SOURCE_DIR}/src/browsers/browser-widget.h
)
target_include_directories(testcore PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(testcore PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets PkgConfig::LIBMPDCLIENT)

# ── Individual tests ──────────────────────────────────────────────────────────

add_executable(test-log test-log.cpp)
target_link_libraries(test-log PRIVATE testcore doctest::doctest)
target_include_directories(test-log PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(test-config test-config.cpp)
target_link_libraries(test-config PRIVATE testcore Qt6::Widgets doctest::doctest)
target_include_directories(test-config PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(test-playlist-model test-playlist-model.cpp)
target_link_libraries(test-playlist-model PRIVATE testcore Qt6::Widgets Qt6::Test doctest::doctest)
target_include_directories(test-playlist-model PRIVATE ${CMAKE_SOURCE_DIR}/src)

# ── Database browser test (uses trompeloeil mocks) ────────────────────────────

add_executable(test-db-browser test-db-browser.cpp
    ${CMAKE_SOURCE_DIR}/src/browsers/database-browser.cpp
)
target_link_libraries(test-db-browser PRIVATE testcore Qt6::Widgets doctest::doctest trompeloeil::trompeloeil)
target_include_directories(test-db-browser PRIVATE ${CMAKE_SOURCE_DIR}/src)

# ── Register with CTest ───────────────────────────────────────────────────────
include(CTest)
enable_testing()
add_test(NAME test-log COMMAND test-log)
add_test(NAME test-config COMMAND test-config)
add_test(NAME test-playlist-model COMMAND test-playlist-model)
add_test(NAME test-db-browser COMMAND test-db-browser)

# ── File browser test ─────────────────────────────────────────────────────────

add_executable(test-file-browser test-file-browser.cpp
    ${CMAKE_SOURCE_DIR}/src/browsers/file-browser.cpp
)
target_link_libraries(test-file-browser PRIVATE testcore Qt6::Widgets doctest::doctest trompeloeil::trompeloeil)
target_include_directories(test-file-browser PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-file-browser COMMAND test-file-browser)

# ── Search browser test ───────────────────────────────────────────────────────

add_executable(test-search-browser test-search-browser.cpp
    ${CMAKE_SOURCE_DIR}/src/browsers/search-browser.cpp
)
target_link_libraries(test-search-browser PRIVATE testcore Qt6::Widgets doctest::doctest trompeloeil::trompeloeil)
target_include_directories(test-search-browser PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-search-browser COMMAND test-search-browser)

# ── Playlist editor test ──────────────────────────────────────────────────────

add_executable(test-playlist-editor test-playlist-editor.cpp
    ${CMAKE_SOURCE_DIR}/src/browsers/playlist-editor.cpp
)
target_link_libraries(test-playlist-editor PRIVATE testcore Qt6::Widgets doctest::doctest trompeloeil::trompeloeil)
target_include_directories(test-playlist-editor PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-playlist-editor COMMAND test-playlist-editor)

# ── Preferences dialog test ────────────────────────────────────────────────────

add_executable(test-preferences-dialog test-preferences-dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/preferences-dialog.cpp)
target_link_libraries(test-preferences-dialog PRIVATE testcore Qt6::Widgets doctest::doctest)
target_include_directories(test-preferences-dialog PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-preferences-dialog COMMAND test-preferences-dialog)

# ── Окно «Add URL»: поле адреса и гаснущая на пустоте кнопка Add ───────────
# Транспорт не участвует: окно отдаёт лишь строку, а команду шлёт вызвавший.

add_executable(test-add-url-dialog test-add-url-dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/add-url-dialog.cpp)
target_link_libraries(test-add-url-dialog PRIVATE Qt6::Widgets doctest::doctest)
target_include_directories(test-add-url-dialog PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-add-url-dialog COMMAND test-add-url-dialog)

# ── Выбор колонок очереди: окно и применение раскладки видом ─────────────────

add_executable(test-column-chooser-dialog test-column-chooser-dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/column-chooser-dialog.cpp)
target_link_libraries(test-column-chooser-dialog PRIVATE Qt6::Widgets doctest::doctest)
target_include_directories(test-column-chooser-dialog PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-column-chooser-dialog COMMAND test-column-chooser-dialog)

add_executable(test-current-playlist-columns test-current-playlist-columns.cpp
    ${CMAKE_SOURCE_DIR}/src/browsers/current-playlist.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/column-chooser-dialog.cpp)
target_link_libraries(test-current-playlist-columns PRIVATE testcore Qt6::Widgets doctest::doctest)
target_include_directories(test-current-playlist-columns PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-current-playlist-columns COMMAND test-current-playlist-columns)

# ── Widget tests ──────────────────────────────────────────────────────────────

add_executable(test-volume-widget test-volume-widget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/volume-widget.cpp)
target_link_libraries(test-volume-widget PRIVATE Qt6::Widgets doctest::doctest)
target_include_directories(test-volume-widget PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-volume-widget COMMAND test-volume-widget)

add_executable(test-progress-slider test-progress-slider.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/progress-slider.cpp)
target_link_libraries(test-progress-slider PRIVATE Qt6::Widgets Qt6::Test doctest::doctest)
target_include_directories(test-progress-slider PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-progress-slider COMMAND test-progress-slider)

add_executable(test-song-info test-song-info.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/song-info.cpp)
target_link_libraries(test-song-info PRIVATE Qt6::Widgets doctest::doctest)
target_include_directories(test-song-info PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-song-info COMMAND test-song-info)

# ── CacheSqlite test ───────────────────────────────────────────────────────────

add_executable(test-cache-sqlite test-cache-sqlite.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/cache-sqlite.cpp)
target_link_libraries(test-cache-sqlite PRIVATE Qt6::Core Qt6::Gui PkgConfig::SQLITE3 doctest::doctest)
target_include_directories(test-cache-sqlite PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-cache-sqlite COMMAND test-cache-sqlite)

# ── Cover widget test ─────────────────────────────────────────────────────────

add_executable(test-cover-widget test-cover-widget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/cover-widget.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/async-download.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/cover-provider.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/metadata-manager.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/cache-sqlite.cpp
    ${CMAKE_SOURCE_DIR}/src/providers/lyrdb.cpp
    ${CMAKE_SOURCE_DIR}/src/providers/lyrics-ovh.cpp
    ${CMAKE_SOURCE_DIR}/src/providers/discogs.cpp)
target_link_libraries(test-cover-widget PRIVATE Qt6::Widgets Qt6::Network
    PkgConfig::SQLITE3
    doctest::doctest)
target_include_directories(test-cover-widget PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-cover-widget COMMAND test-cover-widget)

# ── CoverProvider test (with mock AsyncDownload) ──────────────────────────────

add_executable(test-cover-provider test-cover-provider.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/cover-provider.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/async-download.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/cache-sqlite.cpp
    ${CMAKE_SOURCE_DIR}/src/providers/discogs.cpp)
target_link_libraries(test-cover-provider PRIVATE Qt6::Core Qt6::Gui Qt6::Network
    PkgConfig::SQLITE3
    doctest::doctest)
target_include_directories(test-cover-provider PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-cover-provider COMMAND test-cover-provider)

# ── DeezerProvider test (standalone, mocked) ──────────────────────────────────

add_executable(test-deezer-provider test-deezer-provider.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/cover-provider.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/async-download.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/cache-sqlite.cpp
    ${CMAKE_SOURCE_DIR}/src/providers/discogs.cpp)
target_link_libraries(test-deezer-provider PRIVATE Qt6::Core Qt6::Gui Qt6::Network
    PkgConfig::SQLITE3
    doctest::doctest)
target_include_directories(test-deezer-provider PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-deezer-provider COMMAND test-deezer-provider)

# ── MetadataManager test ──────────────────────────────────────────────────────

add_executable(test-metadata-manager test-metadata-manager.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/metadata-manager.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/cover-provider.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/cache-sqlite.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/async-download.cpp
    ${CMAKE_SOURCE_DIR}/src/providers/lyrdb.cpp
    ${CMAKE_SOURCE_DIR}/src/providers/lyrics-ovh.cpp
    ${CMAKE_SOURCE_DIR}/src/providers/discogs.cpp)
target_link_libraries(test-metadata-manager PRIVATE Qt6::Core Qt6::Gui Qt6::Network
    PkgConfig::SQLITE3
    doctest::doctest)
target_include_directories(test-metadata-manager PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-metadata-manager COMMAND test-metadata-manager)

# ── DiscogsProvider test ────────────────────────────────────────────────────

add_executable(test-discogs-provider test-discogs-provider.cpp
    ${CMAKE_SOURCE_DIR}/src/providers/discogs.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/async-download.cpp)
target_link_libraries(test-discogs-provider PRIVATE Qt6::Core Qt6::Gui Qt6::Network
    doctest::doctest)
target_include_directories(test-discogs-provider PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-discogs-provider COMMAND test-discogs-provider)

# ── MPD_* environment variable test ───────────────────────────────────────

add_executable(test-mpd-env test-mpd-env.cpp
    ${CMAKE_SOURCE_DIR}/src/core/mpd-env.cpp)
target_link_libraries(test-mpd-env PRIVATE Qt6::Core doctest::doctest)
target_include_directories(test-mpd-env PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-mpd-env COMMAND test-mpd-env)

# ── Browser lifecycle (enable on connect / activate on show) ────────────────

add_executable(test-browser-stack test-browser-stack.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/browser-stack.cpp
    ${CMAKE_SOURCE_DIR}/src/browsers/browser-widget.h)
target_link_libraries(test-browser-stack PRIVATE Qt6::Widgets doctest::doctest)
target_include_directories(test-browser-stack PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-browser-stack COMMAND test-browser-stack)

# ── Ошибки libmpdclient и имена путей MPD ────────────────────────────────────

add_executable(test-mpd-error test-mpd-error.cpp)
target_link_libraries(test-mpd-error PRIVATE testcore Qt6::Widgets Qt6::Test
    doctest::doctest PkgConfig::LIBMPDCLIENT)
target_include_directories(test-mpd-error PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-mpd-error COMMAND test-mpd-error)

# ── Движок поиска (сборка шагов протокола, отмена устаревших ответов) ───────

add_executable(test-search-engine test-search-engine.cpp)
target_link_libraries(test-search-engine PRIVATE testcore Qt6::Widgets Qt6::Test
    doctest::doctest PkgConfig::LIBMPDCLIENT)
target_include_directories(test-search-engine PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-search-engine COMMAND test-search-engine)

# ── Слушатель событий idle (разбор маски, жизненный цикл, живое событие) ────

add_executable(test-idle-watcher test-idle-watcher.cpp)
target_link_libraries(test-idle-watcher PRIVATE testcore Qt6::Core Qt6::Test
    doctest::doctest PkgConfig::LIBMPDCLIENT)
target_include_directories(test-idle-watcher PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-idle-watcher COMMAND test-idle-watcher)

# ── Слияние plchanges и синхронизатор очереди ────────────────────────────────
# Чистые кейсы проходят без сервера; интеграционные включаются переменной
# GMPC_QT_TEST_MPD (сокет), а мутационная — дополнительно GMPC_QT_TEST_MPD_MUTATE
# и требует одноразового сервера: она добавляет и удаляет трэки.

add_executable(test-queue-diff test-queue-diff.cpp)
target_link_libraries(test-queue-diff PRIVATE testcore Qt6::Core Qt6::Test
    doctest::doctest PkgConfig::LIBMPDCLIENT)
target_include_directories(test-queue-diff PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-queue-diff COMMAND test-queue-diff)

# ── Команды двойного клика (7.3): play_path / findadd / load / playlistdelete ─
# Без сервера кейсы лишь сообщают о пропуске; мутационные требуют одноразовый
# сервер — они играют очередь, дописывают трэки и правят сохранённый список.

add_executable(test-browser-activation test-browser-activation.cpp)
target_link_libraries(test-browser-activation PRIVATE testcore Qt6::Core
    doctest::doctest PkgConfig::LIBMPDCLIENT)
target_include_directories(test-browser-activation PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-browser-activation COMMAND test-browser-activation)

# ── Контекстные меню браузеров (7.4): состав меню и локальные пути ──────────
# Меню строится из намерений, поэтому проверяется без MPD: что доступно строке, где
# берётся путь для «открыть папку» и в каком порядке удаляются позиции списка.

add_executable(test-browser-menu test-browser-menu.cpp
    ${CMAKE_SOURCE_DIR}/src/browsers/playlist-editor.cpp)
target_link_libraries(test-browser-menu PRIVATE testcore Qt6::Widgets
    doctest::doctest trompeloeil::trompeloeil)
target_include_directories(test-browser-menu PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-browser-menu COMMAND test-browser-menu)

# ── Редактор сохранённых списков (7.5): последовательности команд ───────────
# MpdCommands виртуальный, поэтому мок ловит именно то, что панель отправляет серверу;
# ни одно соединение не нужно — команды подменены.

add_executable(test-playlist-editor-commands test-playlist-editor-commands.cpp
    ${CMAKE_SOURCE_DIR}/src/browsers/playlist-editor.cpp)
target_link_libraries(test-playlist-editor-commands PRIVATE testcore Qt6::Widgets
    doctest::doctest trompeloeil::trompeloeil)
target_include_directories(test-playlist-editor-commands PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-playlist-editor-commands COMMAND test-playlist-editor-commands)

# ── Страница «Now Playing» (8.1): теги, обложка и приход лирики ─────────────
# Менеджер метаданных не поднимается: без него страница живёт на одних тегах,
# а ответ провайдера подаётся прямо в слот — сеть в кейсах не участвует.

add_executable(test-now-playing test-now-playing.cpp
    ${CMAKE_SOURCE_DIR}/src/browsers/now-playing.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/cover-widget.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/metadata-manager.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/async-download.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/cover-provider.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/cache-sqlite.cpp
    ${CMAKE_SOURCE_DIR}/src/providers/lyrdb.cpp
    ${CMAKE_SOURCE_DIR}/src/providers/lyrics-ovh.cpp
    ${CMAKE_SOURCE_DIR}/src/providers/discogs.cpp)
target_link_libraries(test-now-playing PRIVATE testcore Qt6::Widgets Qt6::Network
    PkgConfig::SQLITE3 doctest::doctest trompeloeil::trompeloeil)
target_include_directories(test-now-playing PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-now-playing COMMAND test-now-playing)

# ── Окно «Song Info» (8.2): теги выбранного трэка и его лирика ───────────────
# Менеджер метаданных в кейсах не поднимается: без него окно живёт на одних тегах,
# а приход текста подаётся прямо в слот. Живой кейс проверяет songForUri по
# GMPC_QT_TEST_MPD — единственный путь достать теги строки.

add_executable(test-song-info-dialog test-song-info-dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/song-info-dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/metadata-manager.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/async-download.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/cover-provider.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/cache-sqlite.cpp
    ${CMAKE_SOURCE_DIR}/src/providers/lyrdb.cpp
    ${CMAKE_SOURCE_DIR}/src/providers/lyrics-ovh.cpp
    ${CMAKE_SOURCE_DIR}/src/providers/discogs.cpp)
target_link_libraries(test-song-info-dialog PRIVATE testcore Qt6::Widgets
    Qt6::Network PkgConfig::SQLITE3 doctest::doctest trompeloeil::trompeloeil
    PkgConfig::LIBMPDCLIENT)
target_include_directories(test-song-info-dialog PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-song-info-dialog COMMAND test-song-info-dialog)

# ── Окно «Server Information» (8.3): снимок stats и время по значениям тега ──
# Транспорт подменяется наследником MpdCommands — сервер нужен только живому кейсу
# (GMPC_QT_TEST_MPD), который проверяет разбор настоящих `stats` и `list <tag>`.

add_executable(test-server-info-dialog test-server-info-dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/server-info-dialog.cpp)
target_link_libraries(test-server-info-dialog PRIVATE testcore Qt6::Widgets
    doctest::doctest trompeloeil::trompeloeil PkgConfig::LIBMPDCLIENT)
target_include_directories(test-server-info-dialog PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-server-info-dialog COMMAND test-server-info-dialog)

# ── MPRIS `Metadata`: теги трэка в карте внешнего интерфейса ────────────────
# Сбока полей — чистая функция без DBus: сьюту не нужен ни сессионный шина,
# ни сервер, поэтому он идёт первым из тех, что проверяют MPRIS.

add_executable(test-mpris-metadata test-mpris-metadata.cpp
    ${CMAKE_SOURCE_DIR}/src/mpris/metadata-map.cpp)
target_link_libraries(test-mpris-metadata PRIVATE Qt6::Widgets doctest::doctest)
target_include_directories(test-mpris-metadata PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-mpris-metadata COMMAND test-mpris-metadata)

# ── IPC: локальный roundtrip через QLocalSocket ─────────────────────────────
# Не зависит от testcore — только QLocalServer/QLocalSocket, Qt::Test для spy.

add_executable(test-ipc test-ipc.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ipc.cpp)
target_link_libraries(test-ipc PRIVATE Qt6::Core Qt6::Network Qt6::Test
    doctest::doctest)
target_include_directories(test-ipc PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test-ipc COMMAND test-ipc)
