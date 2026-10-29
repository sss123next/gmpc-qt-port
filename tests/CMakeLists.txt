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
    ${CMAKE_SOURCE_DIR}/src/core/playlist-model.cpp
    ${CMAKE_SOURCE_DIR}/src/browsers/music-database-model.cpp
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
target_link_libraries(test-playlist-model PRIVATE testcore Qt6::Widgets doctest::doctest)
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
