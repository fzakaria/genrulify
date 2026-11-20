#!/usr/bin/env bash
# Summarizes results.jsonl of the whole-nixpkgs run: how each derivation
# ended, how the converted ones were cut, whether their outputs are the
# archives cache.nixos.org has, and how fast the run is going.
#
# usage: stats.sh
set -eu
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
export PATH=$base/roots/jq/bin:$PATH
results=$base/results.jsonl
SECONDS_PER_HOUR=3600

echo "== verdicts"
jq -r .verdict "$results" | sed 's/^build-.*/build-failed/' | sort | uniq -c | sort -rn

echo "== converted, by granularity"
jq -r 'select(.verdict == "ok") | .granularity' "$results" | sort | uniq -c

echo "== converted: every output is the archive the cache has"
jq -r 'select(.verdict == "ok") | if (.outputs | all(.cache == "matches")) then "all outputs match"
       elif (.outputs | any(.cache == "differs")) then "an output differs"
       else "not in the cache" end' "$results" | sort | uniq -c | sort -rn

echo "== converted: process groups"
jq -r 'select(.verdict == "ok") | .summary' "$results" |
    sed -E 's/^([0-9]+) process groups: ([0-9]+) replayed and reproduced, ([0-9]+) raised to copies, ([0-9]+) folded to constants, ([0-9]+) kept as recorded bytes, ([0-9]+) dead.*/\1 \2 \3 \4 \5 \6/' |
    awk '{g+=$1; r+=$2; c+=$3; k+=$4; b+=$5; d+=$6; if ($5 > 0) partial++} END {
        printf "%d groups: %d replayed, %d copies, %d constants, %d recorded bytes, %d dead; %d packages needed recorded bytes\n", g, r, c, k, b, d, partial}'

echo "== rate"
first=$(stat -c %W "$base/locks" 2> /dev/null || echo 0)
if [ "$first" = 0 ]; then
    first=$(stat -c %Y "$base/jobs-curated.txt")
fi
now=$(date +%s)
total=$(wc -l < "$results")
ok=$(jq -r 'select(.verdict == "ok") | .attr' "$results" | wc -l)
echo "$total finished, $ok converted, in $(((now - first) / 60)) minutes: $((ok * SECONDS_PER_HOUR / (now - first + 1))) converted an hour"
