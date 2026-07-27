{
  inputs.self.submodules = true;
  outputs = inputs@{
    self, nixpkgs, flake-parts,
  }: let
    ecc-tools-bin = {
      lib,
      python3Packages,
      rustPlatform,
      stdenv,
      zlib,
      tcl,
      boost,
      eigen,
      yaml-cpp,
      libunwind,
      glog,
      gtest,
      gflags,
      metis,
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
      cargo,
      rustc,
    }: python3Packages.buildPythonPackage rec {
      name = "ecc-tools-bin";
      format = "pyproject";

      src = with lib.fileset; toSource {
        root = ./.;
        fileset = unions [
          ./src
          ./cmake
          ./CMakeLists.txt
          ./pyproject.toml
          ./uv.lock
        ];
      };

      postPatch = lib.pipe {
        sdf_parser = "src/database/manager/parser/sdf/sdf_parse";
        verilog-parser = "src/database/manager/parser/verilog/verilog-rust/verilog-parser";
      } [
        (lib.mapAttrsToList (name: path: ''
          mkdir -p ${path}/.cargo
          cat <<EOF > ${path}/.cargo/config.toml
          [source."crates-io"]
          "replace-with" = "vendored-sources"

          [source."vendored-sources"]
          "directory" = "${rustPlatform.importCargoLock {
            lockFile = "${src}/${path}/Cargo.lock";
          }}"
          EOF
        ''))
        (lib.concatStringsSep "\n")
      ];

      build-system = [
        python3Packages.scikit-build-core
      ];

      dependencies = with python3Packages; [
        torch
      ];

      buildInputs = [
        stdenv.cc.cc.lib
        zlib
        tcl
        boost
        eigen
        yaml-cpp
        libunwind
        glog
        gtest
        gflags
        metis
        gmp
        curl
        tbb_2022
        qhull
      ];
      nativeBuildInputs = [
        cmake
        ninja
        flex
        bison
        patchelf
        pkg-config
        cargo
        rustc
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
