#!/usr/bin/env bash
# Convert one Nix derivation into a Bazel and Buck2 workspace and prove the
# conversion: record the build in Rewind, derive and check the graph, build
# the generated workspace with both systems, and require every archive to
# be the NAR Rewind recorded.
#
# usage: tools/validate.sh INSTALLABLE [OPEN]
#   INSTALLABLE  a flake reference such as nixpkgs#hello, or a .drv path
#   OPEN         programs to treat as driver code, e.g. 'make|bash|sh'
#
# Set GENRULIFY_WORK to a directory an earlier conversion left behind to
# reuse its recording. Recording is the slow part and Rewind makes it
# repeatable, so a change to the compiler can be tried on the recording
# that already exists.
#
# Set GENRULIFY_PASSES to the passes to optimize with, for example `dce`
# for one rule per live process group. Every pass runs by default.
#
# Set GENRULIFY_COARSE to cut the build only at the commands the builder
# itself runs, which leaves `make` as one rule.
#
# Set GENRULIFY_COMPILER to a genrulify binary to use it in place of the
# flake's build, so that a batch of conversions uses one compiler.
set -euo pipefail
installable=${1:?usage: tools/validate.sh INSTALLABLE [OPEN]}
open=${2:-}
root=$(cd "$(dirname "$0")/.." && pwd)
work=${GENRULIFY_WORK:-$(mktemp -d "${TMPDIR:-/tmp}/genrulify.XXXXXXXX")}
echo "Artifacts: $work"

# Pin every tool to the flake's nixpkgs so a rerun uses the same versions.
compiler=${GENRULIFY_COMPILER:-$(nix build --no-link --print-out-paths "$root")/bin/genrulify}
bazel=$(nix build --inputs-from "$root" --no-link --print-out-paths nixpkgs#bazel_7)/bin/bazel
buck=$(nix build --inputs-from "$root" --no-link --print-out-paths nixpkgs#buck2)/bin/buck2

# Record the build. Rewind compares each output with the binary caches and
# reports the NAR hash it built.
# REWIND_ARGS adds to the recording command, for example `--cores 4`.
if [ ! -e "$work/recording/recording.json" ]; then
    # Resolve the derivation. A flake reference is evaluated against the
    # flake's nixpkgs unless it names its own flake.
    case $installable in
        *.drv) drv=$installable ;;
        *) drv=$(nix eval --inputs-from "$root" --raw "$installable.drvPath") ;;
    esac

    # shellcheck disable=SC2086
    rewind nix "$drv" --quiet --json ${REWIND_ARGS:-} > "$work/run.json"
    run=$(jq -er .id "$work/run.json")
    test "$(jq -er .ending "$work/run.json")" = exited:0
    "$compiler" record ${GENRULIFY_COARSE:+--coarse} "$run" "$drv" "$work/recording" ${open:+"$open"}
fi

# Derive the graph and replay every live process group against the
# recording before trusting any of it.
# shellcheck disable=SC2086
"$compiler" check "$work/recording" ${GENRULIFY_PASSES:-}

# One archive per derivation output. A single output is named `archive`.
outputs=$(jq -r '.outputs | keys[]' "$work/recording/recording.json")
count=$(wc -w <<< "$outputs")

# The recording's own view of each output must be what Rewind hashed.
for output in $outputs; do
    path=$(jq -er --arg output "$output" '.outputs[$output]' "$work/recording/recording.json")
    recorded=$(jq -er --arg path "$path" '.outputs[] | select(.path == $path) | .hash' "$work/run.json")
    "$compiler" reference "$work/recording" "$output" "$work/$output.reference.nar"
    test "sha256:$(sha256sum "$work/$output.reference.nar" | cut -d' ' -f1)" = "$recorded"
done

# An earlier validation in the same directory left its workspaces and its
# build logs. This validation makes its own from the recording, which is
# all that is reused.
for old in "$work"/workspace.*; do
    if [ ! -d "$old" ]; then
        continue
    fi
    (cd "$old" && "$buck" kill) > /dev/null 2>&1 || true
    chmod -R u+w "$old"
    rm -rf "$old"
done
rm -f "$work/workspace" "$work"/build.*.log

# What build returns when the graph could not be made or emitted, so that
# no build system ran.
GRAPH_FAILED=2

# Emits the graph the current verdicts give into workspace.$1 and builds
# every output with both systems. Everything the builds print goes to
# build.$1.log, where a failed replay names its rule and what differed.
build() {
    local workspace=$work/workspace.$1
    "$compiler" import "$work/recording" > "$work/recorded.mlir" || return "$GRAPH_FAILED"
    # shellcheck disable=SC2086
    "$compiler" opt --recording "$work/recording" "$work/recorded.mlir" ${GENRULIFY_PASSES:-} > "$work/graph.mlir" || return "$GRAPH_FAILED"
    "$compiler" emit "$work/recording" "$work/graph.mlir" "$workspace" || return "$GRAPH_FAILED"
    (
        cd "$workspace"
        for output in $outputs; do
            target=archive
            if [ "$count" != 1 ]; then
                target=archive_$output
            fi
            "$bazel" --batch build --enable_bzlmod=false "//:$target" || exit 1
            "$compiler" verify "$work/$output.reference.nar" "bazel-bin/$target.nar" || exit 1
            "$buck" build "//:$target" --out "$work/$output.buck.nar" || exit 1
            "$compiler" verify "$work/$output.reference.nar" "$work/$output.buck.nar" || exit 1
        done
    ) > "$work/build.$1.log" 2>&1
    local status=$?

    # Buck2 leaves a daemon behind for every workspace it builds.
    (cd "$workspace" && "$buck" kill) > /dev/null 2>&1 || true

    # Any failure of a build system counts as one failed build.
    if [ "$status" != 0 ]; then
        return 1
    fi
}

# `check` sampled every rule twice. A build samples each one again, inside
# the build system's own sandbox and under its own load, and a rule whose
# output varies can fail there after passing the checks. When that happens
# the failed rule is pinned or demoted from the build log and the graph is
# built again. Every round removes something replay had to reproduce, so
# the loop ends, and it ends on a build where both systems agree.
max_attempts=8
attempt=1
while true; do
    status=0
    build "$attempt" || status=$?
    if [ "$status" = 0 ]; then
        break
    fi

    # Only a rule that failed in a build can be learned from. A graph that
    # could not be emitted has already said why.
    if [ "$status" = "$GRAPH_FAILED" ]; then
        exit 1
    fi
    tail -n 30 "$work/build.$attempt.log"
    "$compiler" learn "$work/recording" < "$work/build.$attempt.log"
    attempt=$((attempt + 1))
    test "$attempt" -le "$max_attempts"
done
ln -s "workspace.$attempt" "$work/workspace"

for output in $outputs; do
    echo "$output: Nix, Bazel and Buck2 agree on sha256:$(sha256sum "$work/$output.reference.nar" | cut -d' ' -f1)"
done
echo "Builds needed: $attempt"
