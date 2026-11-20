#!/usr/bin/env bash
# Starts lanes of the whole-nixpkgs run in the background, at low
# priority: the machine is shared.
#
# usage: start.sh NAME JOBS-FILE LANES
set -eu
name=$1
jobs=$2
lanes=$3
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
here=$(cd "$(dirname "$0")" && pwd)
rm -f "$base/stop"
for lane in $(seq "$lanes"); do
    setsid nohup nice -n 10 "$here/lane.sh" "$jobs" > "$base/logs/lane-$name-$lane.log" 2>&1 < /dev/null &
done
