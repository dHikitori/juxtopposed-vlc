{
  description = "Hakone, a media player based on VLC 4";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    # nixpkgs' Rust is older than the bundled add-ons need and has no
    # wasm32 standard library. Use the release toolchain instead.
    rust-overlay = {
      url = "github:oxalica/rust-overlay";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      rust-overlay,
    }:
    let
      inherit (nixpkgs) lib;
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = f: lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});

      # Hakone's own version, as extras/package/release/version.py reads it.
      version =
        let
          lines = lib.splitString "\n" (builtins.readFile ./meson.build);
          matches = lib.filter (m: m != null) (map (builtins.match "hakone_version = '([^']+)'") lines);
        in
        lib.head (lib.head matches);

      # The add-on toolchain the release builds use.
      rustVersion = lib.fileContents ./extras/package/release/addon-rust-version.txt;

      # A flake already sees only the files git tracks. Leave CI, documentation
      # and the Nix files out as well, so changing them does not rebuild Hakone.
      src = lib.fileset.toSource {
        root = ./.;
        fileset = lib.fileset.difference ./. (
          lib.fileset.unions [
            (lib.fileset.maybeMissing ./.github)
            ./doc
            ./README.md
            ./nix
            ./flake.nix
            (lib.fileset.maybeMissing ./flake.lock)
          ]
        );
      };

      hakoneFor =
        pkgs:
        let
          rustToolchain = (rust-overlay.lib.mkRustBin { } pkgs).stable.${rustVersion}.minimal.override {
            targets = [ "wasm32-unknown-unknown" ];
          };
          medialibrary = pkgs.callPackage ./nix/medialibrary.nix {
            patchDir = ./contrib/src/medialibrary;
          };
        in
        pkgs.callPackage ./nix/package.nix {
          inherit
            src
            version
            medialibrary
            rustToolchain
            ;
        };
    in
    {
      packages = forAllSystems (pkgs: rec {
        hakone = hakoneFor pkgs;
        default = hakone;
      });

      apps = forAllSystems (pkgs: {
        default = {
          type = "app";
          program = lib.getExe self.packages.${pkgs.stdenv.hostPlatform.system}.hakone;
        };
      });

      overlays.default = final: _prev: {
        hakone = hakoneFor final;
      };

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          inputsFrom = [ self.packages.${pkgs.stdenv.hostPlatform.system}.hakone.unwrapped ];
        };
      });

      formatter = forAllSystems (pkgs: pkgs.nixfmt);
    };
}
