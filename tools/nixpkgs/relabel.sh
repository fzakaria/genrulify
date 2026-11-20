#!/usr/bin/env bash
# Gives the derivations Rewind refused as fetchers their own verdict.
# Results written before the harness told them apart say rewind-failed.
set -eu
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
export PATH=$base/roots/jq/bin:$PATH
FIXED_OUTPUT_MARK='is a fixed-output derivation'
flock "$base/results.lock" bash -c '
    while IFS= read -r line; do
        verdict=$(jq -r .verdict <<< "$line")
        key=$(jq -r .drv <<< "$line" | xargs basename | sed "s/\.drv$//")
        if [ "$verdict" = rewind-failed ] && grep -q "$1" "$2/logs/pkg/$key.log" 2> /dev/null; then
            jq -c ".verdict = \"fixed-output\"" <<< "$line"
        else
            printf "%s\n" "$line"
        fi
    done < "$2/results.jsonl" > "$2/results.jsonl.new"
    mv "$2/results.jsonl.new" "$2/results.jsonl"
' relabel "$FIXED_OUTPUT_MARK" "$base"
