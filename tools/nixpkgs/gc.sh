#!/usr/bin/env bash
# Has Rewind collect what the removed runs left behind: the image it
# packed of each build's closure, and pages no keyframe names. Rewind
# refuses while any run executes, so the lanes are held between jobs
# until the last one in flight has ended, and released afterwards.
#
# usage: gc.sh
set -u
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
rewind=$base/roots/rewind/bin/rewind
WAIT_SECONDS=30

# The pattern is in this file so that no shell running the file matches.
jobs_running() { pgrep -u "$(id -u)" -f 'tools/one[.]sh' > /dev/null; }

touch "$base/pause"
trap 'rm -f "$base/pause"' EXIT
while jobs_running; do
    sleep "$WAIT_SECONDS"
done
date
df -h "$base" | tail -1
"$rewind" gc
df -h "$base" | tail -1
