{
  description = "jotawm - minimalist tiling window manager";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in {
      packages = forAllSystems (pkgs: {
        default = pkgs.stdenv.mkDerivation {
          pname = "jotawm";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = [ pkgs.xorg.libX11.dev ];
          buildInputs = [ pkgs.xorg.libX11 ];

          makeFlags = [
            "PREFIX=${placeholder "out"}"
            "X11INC=${pkgs.xorg.libX11.dev}/include"
            "X11LIB=${pkgs.xorg.libX11}/lib"
          ];
        };
      });

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          packages = [ pkgs.gcc pkgs.gnumake pkgs.xorg.libX11.dev ];
        };
      });
    };
}
