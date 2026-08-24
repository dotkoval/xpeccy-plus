{
  description = "Xpeccy+ ZX Spectrum emulator";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs, ... }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];

      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAllSystems (pkgs: {
        default = pkgs.stdenv.mkDerivation {
          pname = "xpeccy-plus";
          version = builtins.replaceStrings [ "\n" "\r" ] [ "" "" ] (builtins.readFile ./VERSION);

          # Build the revision represented by this flake instead of fetching a
          # separate checkout from GitHub.
          src = self;

          nativeBuildInputs = [
            pkgs.cmake
            pkgs.pkg-config
            pkgs.qt6.wrapQtAppsHook
          ];

          buildInputs = [
            pkgs.qt6.qtbase
            pkgs.qt6.qt5compat
            pkgs.SDL2
            pkgs.zlib
          ];

          cmakeFlags = [
            "-DQTVERSION=6"
            "-DSDL1BUILD=0"
          ];

          installPhase = ''
            runHook preInstall
            cmake --install . --prefix "$out"
            runHook postInstall
          '';

          meta = {
            description = "ZX Spectrum emulator";
            homepage = "https://github.com/dotkoval/xpeccy-plus";
            license = pkgs.lib.licenses.gpl3;
            mainProgram = "xpeccy-plus";
          };
        };
      });
    };
}
