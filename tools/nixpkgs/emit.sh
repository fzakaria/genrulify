#!/usr/bin/env bash
# Emits packed recordings of the whole-nixpkgs run into the shared
# workspace, one package each. A recording that was emitted by this
# compiler before is skipped.
#
# usage: emit.sh KEY...
#        emit.sh --all
#
# NIXPKGS_BASE      the batch directory, ~/genrulify-nixpkgs by default
# EMIT_COMPILER     the genrulify to emit with, roots/genrulify-emit by default
# EMIT_JOBS         recordings to emit at once
set -u
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
compiler=${EMIT_COMPILER:-$base/roots/genrulify-emit/bin/genrulify}
workspace=$base/ws
jobs=${EMIT_JOBS:-16}
mkdir -p "$workspace" "$base/emitted" "$base/logs/emit" "$base/tmp"

# One recording: unpack, derive the optimized graph and write the package.
if [ "${1:-}" = --one ]; then
    key=$2
    marker=$base/emitted/$key
    if [ -e "$marker" ]; then
        exit 0
    fi
    scratch=$(mktemp -d "$base/tmp/emit.XXXXXXXX")
    trap 'chmod -R u+w "$scratch" 2> /dev/null; rm -rf "$scratch"' EXIT
    {
        zstd -dc --long=27 "$base/recordings/$key.tar.zst" | tar -C "$scratch" -xf - &&
            "$compiler" import "$scratch/recording" > "$scratch/recorded.mlir" &&
            "$compiler" opt --recording "$scratch/recording" "$scratch/recorded.mlir" > "$scratch/graph.mlir" &&
            "$compiler" emit --into "$workspace" "$scratch/recording" "$scratch/graph.mlir" > "$marker.partial" &&
            mv "$marker.partial" "$marker"
    } > "$base/logs/emit/$key.log" 2>&1 || echo "emit failed: $key" >&2
    exit 0
fi

if [ "${1:-}" = --all ]; then
    set -- $(cd "$base/recordings" && ls | sed 's/\.tar\.zst$//')
fi
printf '%s\n' "$@" | GENRULIFY_JOBS=1 xargs -P "$jobs" -n 1 "$0" --one
