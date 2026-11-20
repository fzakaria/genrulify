#!/usr/bin/env bash
# Keeps the shared workspace up with the lanes: emits the recordings
# that have arrived, links the workspace, builds what is new with Bazel,
# and has Rewind collect its leftovers when the disk runs low, again and
# again until a stop file appears.
#
# usage: loop.sh
set -u
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
here=$(cd "$(dirname "$0")" && pwd)
PAUSE_SECONDS=300

# Free space on the batch directory's disk below which Rewind's leftovers
# are collected.
GC_BELOW_GIGABYTES=${GC_BELOW_GIGABYTES:-900}

# How long after a collection that was given up the next one is tried.
GC_RETRY_SECONDS=7200
next_gc=0
while [ ! -e "$base/stop-loop" ]; do
    "$here/emit.sh" --all
    "$here/link.sh" > "$base/logs/link.log" 2>&1
    "$here/validate.sh" >> "$base/logs/validate.log" 2>&1

    # Rewind keeps an image of every build's closure. Once they have
    # taken enough of the disk, hold the lanes and have Rewind collect
    # the ones whose runs are gone.
    free=$(df --output=avail -BG "$base" | tail -1 | tr -dc 0-9)
    if [ "$free" -lt "$GC_BELOW_GIGABYTES" ] && [ "$(date +%s)" -ge "$next_gc" ]; then
        if ! "$here/gc.sh" >> "$base/logs/gc.log" 2>&1; then
            # A round that was given up held every lane for an hour.
            # Let them work before the next one.
            next_gc=$(($(date +%s) + GC_RETRY_SECONDS))
        fi
    fi
    sleep "$PAUSE_SECONDS"
done
