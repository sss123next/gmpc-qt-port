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
        packages.default = pkgs.callPackage ./packaging/nix/package.nix {
          src = self;
        };

        # ── Development shell (from packaging) ────────────────────────
        devShells.default = pkgs.callPackage ./packaging/nix/dev-shell.nix { };
      }
    );
}
