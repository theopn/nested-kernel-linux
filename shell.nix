{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    gcc
    gnumake
    flex
    bison
    bc
    elfutils
    openssl
    ncurses
    pkg-config
  ];

  ARCH = "x86_64";
}
