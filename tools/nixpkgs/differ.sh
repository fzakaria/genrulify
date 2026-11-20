#!/usr/bin/env bash
# Shows how an output the workspace built differs from the one the cache
# has: unpacks the Bazel-built archive and compares it with the store
# path, file by file.
#
# usage: differ.sh ATTR
set -u
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
export PATH=$base/roots/jq/bin:$PATH
attr=$1
scratch=$(mktemp -d "$base/tmp/differ.XXXXXXXX")
trap 'chmod -R u+w "$scratch"; rm -rf "$scratch"' EXIT
jq -r --arg attr "$attr" 'select(.attr == $attr and .verdict == "ok") | .outputs[] | select(.cache == "differs") | .path' "$base/results.jsonl" |
    while read -r path; do
        name=${path#/nix/store/}
        echo "== $path"
        nix-store --realise "$path" > /dev/null 2>&1
        archive=$(find "$base/ws/bazel-bin/pkgs" -maxdepth 2 -name 'archive*.nar' -path "*${name%-*}*" | head -1)
        for candidate in "$base"/ws/bazel-bin/pkgs/*/archive*.nar; do
            if [ "$(sha256sum < "$candidate" | cut -d' ' -f1)" = "$(jq -r --arg path "$path" 'select(.verdict == "ok") | .outputs[] | select(.path == $path) | .hash' "$base/results.jsonl" | sed 's/^sha256://')" ]; then
                archive=$candidate
                break
            fi
        done
        echo "archive: $archive"
        nix-store --restore "$scratch/built" < "$archive"
        diff -rq "$scratch/built" "$path" | head -20
        chmod -R u+w "$scratch/built"
        rm -rf "$scratch/built"
    done
