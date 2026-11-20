#!/usr/bin/env bash
# Writes the job lists of the whole-nixpkgs run from nix-eval-jobs
# output: every derivation once, under the first attribute that names
# it, in a shuffled order that is the same on every run. A sample taken
# from the front of a shuffled list says something about the whole.
#
# usage: jobs.sh EVAL.jsonl JOBS-ALL
set -eu
SYSTEM_SUFFIX=.x86_64-linux
SHUFFLE_SEED=genrulify-nixpkgs
jq -r 'select(.drvPath != null) | [.attr, .drvPath] | @tsv' "$1" |
    sed "s/$SYSTEM_SUFFIX\t/ /" | sort -u -k2,2 |
    shuf --random-source=<(yes "$SHUFFLE_SEED") > "$2"
