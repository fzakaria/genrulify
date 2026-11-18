#!/usr/bin/env bash
# Record one reference build and verify both lowerings on each backend.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
work=$(mktemp -d "${TMPDIR:-/tmp}/nixsplice.XXXXXXXX")
echo "Artifacts: $work"
compiler=$(nix build --no-link --print-out-paths .)/bin/nixsplice
reference=$(nix build --no-link --print-out-paths .#example)
nix-store --dump "$reference" > "$work/reference.nar"
nix derivation show .#example > "$work/derivation.json"
rewind nix --clock exits --epoch 1 --no-keyframes --no-compare --json .#example > "$work/run.json"
rewind events --json "$(jq -er .id "$work/run.json")" > "$work/events.jsonl"
"$compiler" import "$work/derivation.json" "$work/events.jsonl" > "$work/recorded.mlir"
"$compiler" opt "$work/recorded.mlir" > "$work/folded.mlir"

# Use the flake's tools for every graph so their versions stay comparable.
buck=$(nix build --inputs-from . --no-link --print-out-paths nixpkgs#buck2)/bin/buck2
bazel=$(nix build --inputs-from . --no-link --print-out-paths nixpkgs#bazel_7)/bin/bazel
for graph in recorded folded; do
    "$compiler" emit "$work/$graph.mlir" "$work/$graph"
    (
        cd "$work/$graph"
        "$buck" build //:archive --out "$work/$graph.buck.nar"
        "$compiler" verify "$work/run.json" "$work/reference.nar" "$work/$graph.buck.nar"
        "$bazel" --batch build --enable_bzlmod=false //:archive
        "$compiler" verify "$work/run.json" "$work/reference.nar" bazel-bin/out.nar
    )
done
