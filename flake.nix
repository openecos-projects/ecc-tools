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
      fetchurl,
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
    }: let
      # nixpkgs has not packaged Boost 1.92 yet; override 1.91 with the
      # 1.92.0 source tarball.  CMake requires exactly 1.92.0.
      boost192 = boost191.overrideAttrs (oldAttrs: {
        version = "1.92.0";
        src = fetchurl {
          url = "https://archives.boost.io/release/1.92.0/source/boost_1_92_0.tar.bz2";
          sha256 = "5c1d40cb8e19adbf740a4ec2da35b3e58f3f5804b1dce44deb53df72193cbc6c";
        };
        # Drop the context backport patches — they target < 1.92 / < 1.93
        # and conflict with fixes already present in the 1.92 sources.
        patches = builtins.filter
          (p:
            builtins.isNull (builtins.match ".*0921b9fd.*" (toString p))
            && builtins.isNull (builtins.match ".*58832123.*" (toString p)))
          oldAttrs.patches or [ ];
      });
    in python3Packages.buildPythonPackage rec {
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
        boost192
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
    perSystem = { self', pkgs, system, ... }: {
      packages.default = pkgs.callPackage ecc-tools-bin {};
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
