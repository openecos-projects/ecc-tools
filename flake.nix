{
  inputs.self.submodules = true;
  inputs.nixpkgs-gcc11.url = "github:NixOS/nixpkgs/nixos-24.05";
  outputs = inputs@{
    self, nixpkgs, nixpkgs-gcc11, flake-parts,
  }: let
    ecc-tools-bin = {
      lib,
      python3Packages,
      stdenv,
      zlib,
      tcl,
      boost191,
      eigen,
      libunwind,
      glog,
      gtest,
      gflags,
      gmp,
      curl,
      tbb_2022,
      qhull,
      cmake,
      ninja,
      flex,
      bison,
      patchelf,
      pkg-config,
    }: python3Packages.buildPythonPackage rec {
      name = "ecc-tools-bin";
      format = "pyproject";

      src = with lib.fileset; toSource {
        root = ./.;
        fileset = unions [
          ./src
          ./CMakeLists.txt
          ./pyproject.toml
          ./uv.lock
        ];
      };

      build-system = [
        python3Packages.scikit-build-core
      ];

      dependencies = with python3Packages; [
        matplotlib
      ];

      buildInputs = [
        stdenv.cc.cc.lib
        zlib
        tcl
        boost191
        eigen
        libunwind
        glog
        gtest
        gflags
        gmp
        curl
        tbb_2022
        qhull
        flex
      ];
      nativeBuildInputs = [
        cmake
        ninja
        flex
        bison
        patchelf
        pkg-config
        tcl
      ];
      dontUseCmakeConfigure = true;

      pythonImportsCheck = [ "ecc_tools_bin.ecc_py" ];

      passthru.rawBuildInputs = buildInputs;
      passthru.rawNativeBuildInputs = nativeBuildInputs;
    };
  in flake-parts.lib.mkFlake { inherit inputs; } {
    systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
    perSystem = { self', pkgs, system, config, ... }: let
      boost191 = pkgs.lib.fix (self:
        pkgs.callPackage "${pkgs.path}/pkgs/development/libraries/boost/generic.nix" {
          version = "1.91.0";
          src = pkgs.fetchurl {
            urls = [
              "https://archives.boost.io/release/1.91.0/source/boost_1_91_0.tar.bz2"
            ];
            sha256 = "de5e6b0e4913395c6bdfa90537febd9028ea4c0735d2cdb0cd9b45d5f51264f5";
          };
          # The pinned nixpkgs applies its fix-clang-target.patch to b2 for any
          # boost >= 1.81 with no upper bound, but 1.91 moved --target handling
          # into set-triple, so the hunk no longer applies. nixpkgs master now
          # bounds the patch to [1.81, 1.88); drop it here the same way.
          boost-build = (pkgs.boost-build.override { useBoost = self; }).overrideAttrs (old: {
            patches = builtins.filter (
              p: !pkgs.lib.hasInfix "fix-clang-target" (toString p)
            ) (old.patches or [ ]);
          });
        });
    in let
      # GCC 11 was removed from current nixpkgs; the 24.05 input still has it.
      # Only the compiler stdenv comes from there. Dependencies stay in the
      # main closure, so both uses below are compile/diagnostics gates, not
      # fully consistent link/runtime environments.
      pkgsGcc11 = import inputs.nixpkgs-gcc11 { inherit system; };
      eccTools = self'.packages.default;
    in {
      # Re-export packages and the devShell as checks so CI
      # (`nix flake check`) builds them.
      checks = config.packages // {
        devShell = config.devShells.default;
      };
      packages.default = pkgs.callPackage ecc-tools-bin { inherit boost191; };
      # Toolchain dev environments: the same inputs as `default` under another
      # compiler stdenv. Enter with `nix develop .#gcc13` / `.#gcc11`, then
      # configure a scratch build dir; cmake picks up the shell's CC/CXX.
      devShells = let
        shellArgs = {
          buildInputs = eccTools.rawBuildInputs;
          nativeBuildInputs = eccTools.rawNativeBuildInputs ++ (with pkgs; [ uv ]);
          shellHook = ''
            export CCACHE_DIR="$PWD/.ccache"
          '';
        };
      in {
        default = pkgs.mkShell.override {
          stdenv = pkgs.ccacheStdenv;
        } shellArgs;
        gcc13 = pkgs.mkShell.override {
          stdenv = pkgs.ccacheStdenv.override { stdenv = pkgs.gcc13Stdenv; };
        } shellArgs;
        gcc11 = pkgsGcc11.mkShell.override {
          # ccache must come from the same (24.05) package set: the current
          # nixpkgs ccache wrapper eval fails on gcc11's meta.mainProgram.
          stdenv = pkgsGcc11.ccacheStdenv.override { stdenv = pkgsGcc11.gcc11Stdenv; };
        } shellArgs;
      };
    };
  };
}
