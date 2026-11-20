#!/usr/bin/env bash
# Links the shared workspace of the whole-nixpkgs run: gives every store
# path a package, and every converted attribute an alias at the root.
#
# usage: link.sh
set -eu
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
compiler=${EMIT_COMPILER:-$base/roots/genrulify-emit/bin/genrulify}
export PATH=$base/roots/jq/bin:$PATH

# An attribute and the package that builds it, which is named after the
# store path of the derivation's `out`, or of its first output.
jq -r 'select(.verdict == "ok") | [.attr, ((.outputs | map(.path)) as $paths
        | ($paths | map(select(test("-(dev|lib|bin|man|doc|info|dist|static|debug)$") | not)) | first) // $paths[0])] | @tsv' \
    "$base/results.jsonl" | sed 's|\t/nix/store/| |' > "$base/attrs.txt"
"$compiler" link --attrs "$base/attrs.txt" "$base/ws"
