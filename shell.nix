{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = with pkgs; [
    meson
    ninja
    pkg-config
    SDL2
    libGL
    xorg.libX11
    zlib
  ];
}
