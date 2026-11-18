# A small C project built by stdenv and make, whose source is the directory
# examples/project. The derivation is what `genrulify convert --source
# examples/project .#project OUT` converts in place: the generated build
# files read the files of that directory, so an edit is seen by the next
# build.
{ pkgs }:
pkgs.stdenv.mkDerivation {
  pname = "genrulify-project";
  version = "0.1.0";
  src = ../examples/project;
  makeFlags = [ "PREFIX=$(out)" ];
}
