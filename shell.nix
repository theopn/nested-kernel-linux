let
  pkgs = import <nixpkgs> {
    config = {
      allowUnfree = true;
    };
  };
in
pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    tmux
    gcc
    gnumake
    flex
    bison
    bc
    elfutils
    openssl
    ncurses
    pkg-config

    antigravity
  ];

  ARCH = "x86_64";
}
