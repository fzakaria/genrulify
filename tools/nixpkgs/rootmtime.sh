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

# A build that has not ended after this long is abandoned. The slowest
# first build of the run took a little over half an hour.
BUILD_TIMEOUT=${BUILD_TIMEOUT:-2h}

mkdir -p "$base/roots/rootmtime-inputs"
for attr in "$@"; do
    # The result of the first build names the derivation, what the cache
    # said of each output, and the VM memory the build needed. A build
    # given less memory than that fills /build and never ends.
    result=$(jq -c --arg attr "$attr" 'select(.attr == $attr)' "$base/results.jsonl" | head -1)
    drv=$(jq -r .drv <<< "$result")
    before=$(jq -c '[.outputs[].cache]' <<< "$result")
    memory=$(jq -r .memory <<< "$result")

    nix-store --query --references "$drv" |
        xargs nix-store --realise --add-root "$base/roots/rootmtime-inputs/r" --indirect > /dev/null 2>&1

    # Rewind stops the VM and marks the run interrupted when timeout
    # signals it, and prints no JSON then.
    run=$(timeout "$BUILD_TIMEOUT" "$rewind" nix "$drv" --mem "$memory" --quiet --json 2> /dev/null)
    if [ -z "$run" ]; then
        echo "$attr: before $before, with a dated root did not finish"
        continue
    fi

    after=$(jq -c '[.outputs[] | ([.comparisons[] | select(.source == "https://cache.nixos.org") | .verdict] | first)]' <<< "$run")
    echo "$attr: before $before, with a dated root $after"
    "$rewind" remove "$(jq -r .id <<< "$run")" > /dev/null 2>&1
done
