{
  inputs.self.submodules = true;
  outputs = inputs@{
    self, nixpkgs, flake-parts,
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
        torch
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
    perSystem = { self', pkgs, system, ... }: let
      boost191 = pkgs.lib.fix (self:
        pkgs.callPackage "${pkgs.path}/pkgs/development/libraries/boost/generic.nix" {
          version = "1.91.0";
          src = pkgs.fetchurl {
            urls = [
              "https://archives.boost.io/release/1.91.0/source/boost_1_91_0.tar.bz2"
            ];
            sha256 = "de5e6b0e4913395c6bdfa90537febd9028ea4c0735d2cdb0cd9b45d5f51264f5";
          };
          # nixpkgs' fix-clang-target.patch targets the pre-1.91 clang.jam
          # layout and fails during the Boost 1.91 source unpack. Boost 1.91
          # already carries the current clang target handling, so keep the
          # matching b2 source but omit that stale nixpkgs patch.
          boost-build = (pkgs.boost-build.override {
            useBoost = self;
          }).overrideAttrs (_: {
            patches = [];
          });
        });
    in {
      packages.default = pkgs.callPackage ecc-tools-bin { inherit boost191; };
      devShells.default = pkgs.mkShell.override {
        stdenv = pkgs.ccacheStdenv;
      } {
        buildInputs = self'.packages.default.rawBuildInputs;
        nativeBuildInputs = self'.packages.default.rawNativeBuildInputs ++ (with pkgs; [ uv ]);
        shellHook = ''
          export CCACHE_DIR="$PWD/.ccache"
        '';
      };
    };
  };
}
