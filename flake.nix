{
  description = "screenshot, screen-recording, ocr, and uploader for wlroots wayland compositors.";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    utils.url = "github:numtide/flake-utils";
  };

  outputs = {
    self,
    nixpkgs,
    utils,
  }:
    utils.lib.eachDefaultSystem (system: let
      pkgs = import nixpkgs {inherit system;};
    in {
      packages.default = pkgs.stdenv.mkDerivation rec {
        pname = "grabit";
        version = "0.2.0";

        src = ./.;

        nativeBuildInputs = with pkgs; [
          pkg-config
          wayland-scanner
          makeWrapper
        ];

        buildInputs = with pkgs; [
          json_c
          curl
          file
          wayland
          wayland-protocols
          cairo
          libxkbcommon
          systemd.dev
        ];

        runtimeDeps = with pkgs; [
          ffmpeg
          tesseract
        ];

        installPhase = ''
          runHook preInstall
          mkdir -p $out/bin
          make install PREFIX=$out
          runHook postInstall
        '';

        postFixup = ''
          wrapProgram $out/bin/grabit \
            --prefix PATH : ${pkgs.lib.makeBinPath runtimeDeps}
        '';

        meta = with pkgs.lib; {
          description = "screenshot, screen-recording, ocr, and uploader for wlroots wayland compositors.";
          homepage = "https://heliopolis.live/creations/grabit-c";
          license = licenses.agpl3Plus;
          platforms = platforms.linux;
          mainProgram = "grabit";
        };
      };

      devShells.default = pkgs.mkShell {
        inputsFrom = [self.packages.${system}.default];
        buildInputs = with pkgs; [
          clang-tools
          bear
          gdb
        ];
      };
    });
}
