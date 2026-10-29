{
  description = "gmpc-qt — Modern MPD client (Qt port of gmpc)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config.allowAliases = false;  # catch removed aliases like qt6.full
        };
        inherit (pkgs) qt6;
      in {
        devShells.default = pkgs.mkShell {
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
        };

        # Also expose a package derivation for nix build / nix run
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "gmpc-qt";
          version = "0.1.0";
          src = self;

          nativeBuildInputs = with pkgs; [ cmake pkg-config qt6.wrapQtAppsHook ];

          buildInputs = with pkgs; [
            qt6.qtbase
            qt6.qtsvg
            qt6.qttools
            libmpdclient
            curl
            openssl
            sqlite
          ];

          meta.mainProgram = "gmpc-qt";
        };
      }
    );
}
