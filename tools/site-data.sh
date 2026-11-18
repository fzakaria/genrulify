#!/usr/bin/env bash
# Regenerate the data one package contributes to the site from a checked
# recording: the optimized graph as compressed MLIR and as JSON, and both
# build files.
#
# usage: tools/site-data.sh RECORDING ID
#   RECORDING  a directory `genrulify record` and `genrulify check` wrote
#   ID         the directory name under site/data, e.g. hello
set -euo pipefail
recording=${1:?usage: tools/site-data.sh RECORDING ID}
id=${2:?usage: tools/site-data.sh RECORDING ID}
root=$(cd "$(dirname "$0")/.." && pwd)
out=$root/site/data/$id
work=$(mktemp -d)
trap 'chmod -R u+w "$work" && rm -rf "$work"' EXIT

# The build files name the compiler by path, so use the flake's build and
# not whatever is in a development tree.
compiler=$(nix build --no-link --print-out-paths "$root")/bin/genrulify

"$compiler" import "$recording" > "$work/recorded.mlir"
"$compiler" opt "$work/recorded.mlir" > "$work/graph.mlir"
"$compiler" emit "$recording" "$work/graph.mlir" "$work/workspace"
rm -rf "$out"
mkdir -p "$out"
"$compiler" describe "$recording" "$work/graph.mlir" > "$out/graph.json"
cp "$work/workspace/BUILD.bazel" "$work/workspace/BUCK" "$out/"

# A group that was given a whole source tree prints thousands of operands,
# so the graph of a large package runs to megabytes of text. The page shows
# each operation from graph.json and only links to the whole module.
gzip --best --no-name --stdout "$work/graph.mlir" > "$out/graph.mlir.gz"

# The site shows the NAR hash every build agreed on.
"$compiler" reference "$recording" out "$work/out.nar"
printf 'sha256:%s\n' "$(sha256sum "$work/out.nar" | cut -d' ' -f1)" > "$out/nar-hash"
