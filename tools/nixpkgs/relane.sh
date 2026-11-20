#!/usr/bin/env bash
# Replaces the running lanes and the loop with ones that run the current
# scripts. The job each old lane has in flight is left to finish.
#
# usage: relane.sh LANES
set -u
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
here=$(cd "$(dirname "$0")" && pwd)

# The patterns are in this file so that no shell running the file matches.
pkill -u "$(id -u)" -f 'tools/lane[.]sh'
pkill -u "$(id -u)" -f 'tools/loop[.]sh'
sleep 1
MAX_EXECS=1500 RECORD_TIMEOUT=900 GENRULIFY_JOBS=4 "$here/start.sh" "all-$(date +%H%M)" "$base/jobs-all.txt" "$1"
MAX_EXECS=40000 RECORD_TIMEOUT=3600 SNAPSHOT_TIMEOUT=7200 GENRULIFY_JOBS=6 "$here/start.sh" "curated-$(date +%H%M)" "$base/jobs-curated.txt" 4
setsid nohup nice -n 5 "$here/loop.sh" > "$base/logs/loop.log" 2>&1 < /dev/null &
