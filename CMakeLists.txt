cmake_minimum_required(VERSION 3.20)
project(gmpc-qt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines")  # for C++20 coroutines if used later
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)  # no .ui files — pure code layout

# ── Dependencies ──────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Svg DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBMPDCLIENT REQUIRED IMPORTED_TARGET libmpdclient)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(XCB_KEYSYMS REQUIRED IMPORTED_TARGET xcb-keysyms)

# ── Sources ───────────────────────────────────────────────────────────────────
set(CORE_SOURCES
    src/core/connection.cpp
    src/core/connection.h
    src/core/mpd-error.cpp
    src/core/mpd-error.h
    src/core/mpd-endpoint.cpp
    src/core/mpd-endpoint.h
    src/core/mpd-commands.cpp
    src/core/mpd-commands.h
    src/core/config.cpp
    src/core/config.h
    src/core/mpd-env.cpp
    src/core/mpd-env.h
    src/core/search-engine.cpp
    src/core/search-engine.h
    src/core/playlist-model.cpp
    src/core/playlist-model.h
)

set(GUI_SOURCES
    src/gui/mainwindow.cpp
    src/gui/mainwindow.h
    src/gui/browser-stack.cpp
    src/gui/browser-stack.h
    src/gui/controlbar.cpp
    src/gui/controlbar.h
    src/gui/statusbar.cpp
    src/gui/statusbar.h
    src/gui/trayicon.cpp
    src/gui/trayicon.h
    src/gui/preferences-dialog.cpp
    src/gui/preferences-dialog.h
)

set(BROWSER_SOURCES
    src/browsers/browser-widget.h
    src/browsers/current-playlist.cpp
    src/browsers/current-playlist.h
    src/browsers/file-browser.cpp
    src/browsers/file-browser.h
    src/browsers/database-browser.cpp
    src/browsers/database-browser.h
    src/browsers/music-database-model.cpp
    src/browsers/music-database-model.h
    src/browsers/file-system-model.cpp
    src/browsers/file-system-model.h
    src/browsers/search-browser.cpp
    src/browsers/search-browser.h
    src/browsers/playlist-editor.cpp
    src/browsers/playlist-editor.h
)

set(METADATA_SOURCES
    src/metadata/metadata-manager.cpp
    src/metadata/metadata-manager.h
    src/metadata/cache-sqlite.cpp
    src/metadata/cache-sqlite.h
    src/metadata/cover-provider.cpp
    src/metadata/cover-provider.h
)

set(PROVIDER_SOURCES
    src/providers/lastfm.cpp
    src/providers/lastfm.h
    src/providers/discogs.cpp
    src/providers/discogs.h
    src/providers/lyrdb.cpp
    src/providers/lyrdb.h
    src/providers/lyrics-ovh.cpp
    src/providers/lyrics-ovh.h
    src/providers/musicbrainz.cpp
    src/providers/musicbrainz.h
)

set(WIDGET_SOURCES
    src/widgets/volume-widget.cpp
    src/widgets/volume-widget.h
    src/widgets/progress-slider.cpp
    src/widgets/progress-slider.h
    src/widgets/song-info.cpp
    src/widgets/song-info.h
    src/widgets/cover-widget.cpp
    src/widgets/cover-widget.h
)

set(TOOL_SOURCES
    src/tools/log.cpp
    src/tools/log.h
    src/tools/async-download.cpp
    src/tools/async-download.h
)

set(MPRIS_SOURCES
    src/mpris/mpris-player.cpp
    src/mpris/mpris-player.h
)

set(MAIN_SOURCE
    src/main.cpp
)

# ── Target ────────────────────────────────────────────────────────────────────
qt_add_executable(gmpc-qt
    ${MAIN_SOURCE}
    ${CORE_SOURCES}
    ${GUI_SOURCES}
    ${BROWSER_SOURCES}
    ${METADATA_SOURCES}
    ${PROVIDER_SOURCES}
    ${WIDGET_SOURCES}
    ${TOOL_SOURCES}
    ${MPRIS_SOURCES}
)

target_include_directories(gmpc-qt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(gmpc-qt PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
    Qt6::DBus
    PkgConfig::LIBMPDCLIENT
    PkgConfig::SQLITE3
    PkgConfig::CURL
    PkgConfig::XCB_KEYSYMS
)

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS gmpc-qt RUNTIME DESTINATION bin)

# ── Tests (opt-in) ────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    find_package(doctest REQUIRED)
    find_package(trompeloeil REQUIRED)
    add_subdirectory(tests)
endif()

# ── Doxygen target (optional) ─────────────────────────────────────────────────
find_program(DOXYGEN doxygen)
if(DOXYGEN)
    configure_file(Doxyfile.in "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile" @ONLY)
    add_custom_target(doc
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/docs/doxygen"
        COMMAND ${DOXYGEN} "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating API documentation with Doxygen"
    )
endif()
