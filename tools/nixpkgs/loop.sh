#!/usr/bin/env bash
# Keeps the shared workspace up with the lanes: emits the recordings
# that have arrived, links the workspace and builds what is new with
# Bazel, again and again until a stop file appears.
#
# usage: loop.sh
set -u
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
here=$(cd "$(dirname "$0")" && pwd)
PAUSE_SECONDS=300
while [ ! -e "$base/stop-loop" ]; do
    "$here/emit.sh" --all
    "$here/link.sh" > "$base/logs/link.log" 2>&1
    "$here/validate.sh" >> "$base/logs/validate.log" 2>&1
    sleep "$PAUSE_SECONDS"
done
