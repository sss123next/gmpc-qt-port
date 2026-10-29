{ stdenv, cmake, pkg-config, qt6, libmpdclient, curl, openssl, sqlite, src }:

stdenv.mkDerivation {
  pname = "gmpc-qt";
  version = "0.1.0";
  inherit src;

  nativeBuildInputs = [ cmake pkg-config qt6.wrapQtAppsHook ];

  buildInputs = [
    qt6.qtbase
    qt6.qtsvg
    qt6.qttools
    libmpdclient
    curl
    openssl
    sqlite
  ];

  meta.mainProgram = "gmpc-qt";
}
