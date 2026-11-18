{ pkgs }:
let
  qt6 = pkgs.qt6;
in

pkgs.mkShell {
  name = "gmpc-qt";

  packages = with pkgs; [
    # -- Build toolchain --
    cmake
    gnumake
    pkg-config

    # -- Qt6 (individual modules for minimal closure) --
    qt6.qtbase           # Core, Gui, Widgets, Network
    qt6.qtsvg            # SVG icon/cover rendering
    qt6.qttools          # lupdate, lrelease, linguist
    qt6.qtwayland        # Wayland platform plugin
    qt6.qttranslations   # i18n

    # -- MPD client --
    libmpdclient

    # -- HTTP / TLS --
    curl
    openssl

    # -- SQLite (for metadata cache) --
    sqlite

    # -- Tag parsing (embedded cover art & lyrics) --
    taglib

    # -- Tests --
    doctest               # header-only test framework
    trompeloeil            # header-only mock framework

    # -- Dev tooling --
    gdb
    valgrind
    strace

    # -- Docs --
    doxygen
    graphviz
  ];

  nativeBuildInputs = with pkgs; [
    qt6.wrapQtAppsHook
  ];

  shellHook = ''
    echo "=== gmpc-qt dev shell ==="
    echo "Qt:     $(qmake6 --version 2>&1 | head -1)"
    echo "CMake:  $(cmake --version | head -1)"
    echo "MPD:    $(pkg-config --modversion libmpdclient 2>/dev/null || echo 'not found')"
    echo "========================="
  '';
}
