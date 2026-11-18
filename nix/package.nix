{ pkgs }:
pkgs.stdenv.mkDerivation {
  pname = "genrulify";
  version = "0.1.0";
  src = pkgs.lib.fileset.toSource {
    root = ../.;
    fileset = pkgs.lib.fileset.unions [
      ../CMakeLists.txt
      ../src
      ../tests
    ];
  };
  nativeBuildInputs = [
    pkgs.cmake
    pkgs.ninja
    pkgs.llvmPackages.tblgen
  ];
  buildInputs = [
    pkgs.llvmPackages.mlir
    pkgs.llvmPackages.llvm
    pkgs.abseil-cpp
    pkgs.nlohmann_json
  ];
  SPLICE_BWRAP = pkgs.bubblewrap;
  SPLICE_TAR = pkgs.gnutar;
  SPLICE_BASH = pkgs.bash;
  SPLICE_COREUTILS = pkgs.coreutils;
  SPLICE_NIX = pkgs.nix;
  SPLICE_STRACE = pkgs.strace;
  doCheck = true;
}
