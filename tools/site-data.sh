#!/usr/bin/env bash
# Regenerate the data one package contributes to the site from a checked
# recording: the optimized graph as MLIR and as JSON, and both build files.
#
# usage: tools/site-data.sh RECORDING ID
#   RECORDING  a directory `nixsplice record` and `nixsplice check` wrote
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
compiler=$(nix build --no-link --print-out-paths "$root")/bin/nixsplice

"$compiler" import "$recording" > "$work/recorded.mlir"
"$compiler" opt "$work/recorded.mlir" > "$work/graph.mlir"
"$compiler" emit "$recording" "$work/graph.mlir" "$work/workspace"
rm -rf "$out"
mkdir -p "$out"
"$compiler" describe "$recording" "$work/graph.mlir" > "$out/graph.json"
cp "$work/graph.mlir" "$work/workspace/BUILD.bazel" "$work/workspace/BUCK" "$out/"

# The site shows the NAR hash every build agreed on.
"$compiler" reference "$recording" out "$work/out.nar"
printf 'sha256:%s\n' "$(sha256sum "$work/out.nar" | cut -d' ' -f1)" > "$out/nar-hash"
