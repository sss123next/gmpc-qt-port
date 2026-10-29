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
          config.allowAliases = false;
        };
      in {
        # ── Installable package ──────────────────────────────────────────
        packages.default = pkgs.callPackage ./build-aux/nix/package.nix {
          src = self;
        };

        # ── Development shell (from build-aux) ───────────────────────────
        devShells.default = pkgs.callPackage ./build-aux/nix/dev-shell.nix { };
      }
    );
}
