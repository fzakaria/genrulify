#!/usr/bin/env bash
# Runs rootmtime.sh over every converted derivation that had an output
# differing from the cache's, several at a time, and counts how many
# have every output matching once the job's root is dated.
#
# usage: rootmtime-all.sh
set -u
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
export PATH=$base/roots/jq/bin:$PATH
PARALLEL=${PARALLEL:-16}
out=$base/logs/rootmtime.txt
jq -r 'select(.verdict == "ok" and (.outputs | any(.cache == "differs"))) | .attr' "$base/results.jsonl" |
    xargs -P "$PARALLEL" -n 1 "$base/tools/rootmtime.sh" > "$out" 2>&1
echo "$(grep -c 'with a dated root' "$out") rebuilt"
echo "$(grep -c 'with a dated root \["matches"\(,"matches"\)*\]$' "$out") now match in every output"
grep -v 'with a dated root \["matches"\(,"matches"\)*\]$' "$out" | cut -c1-200
