#!/usr/bin/env bash
# Regenerate the data one package contributes to the site from a checked
# recording: the optimized graph as compressed MLIR, a compressed JSON
# description of the graph under every choice of passes, and both build
# files.
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

# Use the flake's build of the compiler and not whatever is in a
# development tree, unless a batch has pinned one.
compiler=${GENRULIFY_COMPILER:-$(nix build --no-link --print-out-paths "$root")/bin/genrulify}

"$compiler" import "$recording" > "$work/recorded.mlir"
"$compiler" opt --recording "$recording" "$work/recorded.mlir" > "$work/graph.mlir"
"$compiler" emit "$recording" "$work/graph.mlir" "$work/workspace"
rm -rf "$out"
mkdir -p "$out"

# The description is made from the module before optimization and holds
# the graph under every choice of passes, so the page can switch them on
# and off. It runs to megabytes for a large package and compresses well.
"$compiler" describe "$recording" "$work/recorded.mlir" |
    gzip --best --no-name > "$out/graph.json.gz"
cp "$work/workspace/BUILD.bazel" "$work/workspace/BUCK" "$out/"

# A group that was given a whole source tree prints thousands of operands,
# so the graph of a large package runs to megabytes of text. The page shows
# each operation from graph.json and only links to the whole module.
gzip --best --no-name --stdout "$work/graph.mlir" > "$out/graph.mlir.gz"

# The site shows the NAR hash every build agreed on.
"$compiler" reference "$recording" out "$work/out.nar"
printf 'sha256:%s\n' "$(sha256sum "$work/out.nar" | cut -d' ' -f1)" > "$out/nar-hash"
