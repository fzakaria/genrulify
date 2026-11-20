#!/usr/bin/env bash
# Times one Rewind query against the newest finished run, with and
# without a cap on Rewind's worker threads.
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
R=$base/roots/rewind/bin/rewind
pgrep -u "$(id -u)" -c rewind
run=$($R ls | grep 'exited:0' | head -1 | cut -d' ' -f1)
echo "run $run"
for setting in "X=1" "RAYON_NUM_THREADS=1"; do
    echo "env: $setting"
    ( time env "$setting" "$R" --progress off cat --archive tar "$run" 2000 /build > /dev/null ) 2>&1 | grep -v '^$'
done
uptime
