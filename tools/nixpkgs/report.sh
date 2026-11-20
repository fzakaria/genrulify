#!/usr/bin/env bash
# Writes report.json: every number the README of the published workspace
# and the write-up quote, read from the run's own files.
#
# usage: report.sh
set -eu
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
export PATH=$base/roots/jq/bin:$PATH
results=$base/results.jsonl
ws=$base/ws

# The macros of genrulify.bzl that are one genrule each.
RULE_CALL='^(recorded|cc_object|cc_library|cc_binary|cc_shared_library|rust_library|rust_binary|rust_proc_macro|nix_archive)\($'

count() { jq -r "$1" "$results" | grep -c . || true; }
link_count() { sed -n "s/^ *\([0-9]*\) $1.*/\1/p" "$base/logs/link.log" | tail -1; }
packages=$(find "$ws/pkgs" -mindepth 1 -maxdepth 1 -type d ! -name '.*' | wc -l)
jq -n \
    --argjson derivations "$(wc -l < "$base/jobs-all.txt")" \
    --argjson finished "$(wc -l < "$results")" \
    --argjson converted "$(count 'select(.verdict == "ok") | .attr')" \
    --argjson fine "$(count 'select(.verdict == "ok" and .granularity == "fine") | .attr')" \
    --argjson coarse "$(count 'select(.verdict == "ok" and .granularity == "coarse") | .attr')" \
    --argjson build_timed_out "$(count 'select(.verdict == "build-timed-out") | .attr')" \
    --argjson build_failed "$(count 'select((.verdict | startswith("build-")) and .verdict != "build-timed-out") | .attr')" \
    --argjson no_inputs "$(count 'select(.verdict == "no-inputs") | .attr')" \
    --argjson fixed_output "$(count 'select(.verdict == "fixed-output") | .attr')" \
    --argjson timed_out "$(count 'select(.verdict == "record-timeout" or .verdict == "check-timeout") | .attr')" \
    --argjson other_failed "$(count 'select(.verdict == "record-failed" or .verdict == "check-failed" or .verdict == "rewind-failed") | .attr')" \
    --argjson all_match "$(count 'select(.verdict == "ok" and (.outputs | all(.cache == "matches"))) | .attr')" \
    --argjson some_differ "$(count 'select(.verdict == "ok" and (.outputs | any(.cache == "differs"))) | .attr')" \
    --argjson packages "$packages" \
    --argjson rules "$(cat "$ws"/pkgs/*/BUILD.bazel | grep -Ec "$RULE_CALL")" \
    --argjson validated "$(grep -lx true "$base"/validated/* 2> /dev/null | wc -l)" \
    --argjson validation_failed "$(grep -lx false "$base"/validated/* 2> /dev/null | wc -l)" \
    --argjson store_paths "$(find "$ws/store" -mindepth 1 -maxdepth 1 -type d | wc -l)" \
    --argjson built_paths "$(link_count 'built by a package')" \
    --argjson fetched_paths "$(link_count 'fetched from')" \
    --argjson kept_paths "$(link_count 'kept as an archive')" \
    --argjson differing_outputs "$(sed -n 's/^\([0-9]*\) outputs\? differs\?.*/\1/p' "$base/logs/link.log" | tail -1 | grep . || echo 0)" \
    --arg nixpkgs "$(cat "$base/eval/src" | xargs basename)" \
    --arg date "$(date -u +%Y-%m-%d)" \
    '$ARGS.named' > "$base/report.json"
cat "$base/report.json"
