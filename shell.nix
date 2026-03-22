{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = with pkgs; [
    meson
    ninja
    pkg-config
    sdl3
    libGL
    xorg.libX11
    zlib
  ];
}
