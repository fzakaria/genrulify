#!/usr/bin/env bash
# Convert one Nix derivation into a Bazel and Buck2 workspace and prove the
# conversion: record the build in Rewind, derive and check the graph, build
# the generated workspace with both systems, and require every archive to
# be the NAR Rewind recorded.
#
# usage: tools/convert.sh INSTALLABLE [OPEN]
#   INSTALLABLE  a flake reference such as nixpkgs#hello, or a .drv path
#   OPEN         programs to treat as driver code, e.g. 'make|bash|sh'
set -euo pipefail
installable=${1:?usage: tools/convert.sh INSTALLABLE [OPEN]}
open=${2:-}
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/nixsplice.XXXXXXXX")
echo "Artifacts: $work"

# Pin every tool to the flake's nixpkgs so a rerun uses the same versions.
compiler=$(nix build --no-link --print-out-paths "$root")/bin/nixsplice
bazel=$(nix build --inputs-from "$root" --no-link --print-out-paths nixpkgs#bazel_7)/bin/bazel
buck=$(nix build --inputs-from "$root" --no-link --print-out-paths nixpkgs#buck2)/bin/buck2

# Resolve the derivation. A flake reference is evaluated against the
# flake's nixpkgs unless it names its own flake.
case $installable in
    *.drv) drv=$installable ;;
    *) drv=$(nix eval --inputs-from "$root" --raw "$installable.drvPath") ;;
esac

# Record the build. Rewind compares each output with the binary caches and
# reports the NAR hash it built.
rewind nix "$drv" --quiet --json > "$work/run.json"
run=$(jq -er .id "$work/run.json")
test "$(jq -er .ending "$work/run.json")" = exited:0

# Derive the graph and replay every live process group against the
# recording before trusting any of it.
"$compiler" record "$run" "$drv" "$work/recording" ${open:+"$open"}
"$compiler" check "$work/recording"
"$compiler" import "$work/recording" > "$work/recorded.mlir"
"$compiler" opt "$work/recorded.mlir" > "$work/graph.mlir"
"$compiler" emit "$work/recording" "$work/graph.mlir" "$work/workspace"

# One archive per derivation output. A single output is named `archive`.
outputs=$(jq -r '.outputs | keys[]' "$work/recording/recording.json")
count=$(wc -w <<< "$outputs")
cd "$work/workspace"
for output in $outputs; do
    target=archive
    if [ "$count" != 1 ]; then
        target=archive_$output
    fi

    # The recording's own view of the output must be what Rewind hashed.
    path=$(jq -er --arg output "$output" '.outputs[$output]' "$work/recording/recording.json")
    recorded=$(jq -er --arg path "$path" '.outputs[] | select(.path == $path) | .hash' "$work/run.json")
    "$compiler" reference "$work/recording" "$output" "$work/$output.reference.nar"
    test "sha256:$(sha256sum "$work/$output.reference.nar" | cut -d' ' -f1)" = "$recorded"

    "$bazel" --batch build --enable_bzlmod=false "//:$target"
    "$compiler" verify "$work/$output.reference.nar" "bazel-bin/$output.nar"
    "$buck" build "//:$target" --out "$work/$output.buck.nar"
    "$compiler" verify "$work/$output.reference.nar" "$work/$output.buck.nar"
    echo "$output: Nix, Bazel and Buck2 agree on $recorded"
done
"$buck" kill > /dev/null 2>&1 || true
