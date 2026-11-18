{ pkgs }:
pkgs.stdenv.mkDerivation {
  pname = "nixsplice";
  version = "0.1.0";
  src = ../.;
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
  doCheck = true;
}
