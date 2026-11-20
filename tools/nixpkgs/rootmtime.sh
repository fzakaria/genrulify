#!/usr/bin/env bash
# Builds derivations with a Rewind whose guest dates the job's root, and
# prints for each output whether it is now the archive cache.nixos.org
# has. For trying a Rewind change against outputs that differed.
#
# usage: rootmtime.sh ATTR...
set -u
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
export PATH=$base/roots/jq/bin:$PATH
rewind=$base/roots/rewind-rootmtime/bin/rewind
mkdir -p "$base/roots/rootmtime-inputs"
for attr in "$@"; do
    drv=$(jq -r --arg attr "$attr" 'select(.attr == $attr) | .drv' "$base/results.jsonl" | head -1)
    before=$(jq -c --arg attr "$attr" 'select(.attr == $attr) | [.outputs[].cache]' "$base/results.jsonl" | head -1)
    nix-store --query --references "$drv" |
        xargs nix-store --realise --add-root "$base/roots/rootmtime-inputs/r" --indirect > /dev/null 2>&1
    run=$("$rewind" nix "$drv" --quiet --json 2> /dev/null)
    after=$(jq -c '[.outputs[] | ([.comparisons[] | select(.source == "https://cache.nixos.org") | .verdict] | first)]' <<< "$run")
    echo "$attr: before $before, with a dated root $after"
    "$rewind" remove "$(jq -r .id <<< "$run")" > /dev/null 2>&1
done
