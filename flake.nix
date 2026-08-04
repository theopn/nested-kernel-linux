{
  description = "Theo's Nested Kernel Dev Env";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";

      pkgs = import nixpkgs {
        inherit system;
        config = {
          allowUnfree = true;
        };
      };
    in
    {
      devShells.${system}.default = pkgs.mkShell {
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

          antigravity-ide
        ];

        ARCH = "x86_64";
      };
    };
}
