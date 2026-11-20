#!/usr/bin/env bash
# Traces one Rewind query under the current load and prints where its
# wall time went, by system call.
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
R=$base/roots/rewind/bin/rewind
run=$($R ls | grep 'exited:0' | head -1 | cut -d' ' -f1)
steps=$($R ls | grep 'exited:0' | head -1 | awk '{print $3}')
step=$((steps / 2))
echo "run $run step $step"
strace=$(nix build --no-link --print-out-paths nixpkgs#strace 2> /dev/null)/bin/strace
time "$R" --progress off cat "$run" "$step" /proc/1/cmdline > /dev/null
"$strace" -f -c -w -o "$base/tmp/query.strace" "$R" --progress off cat "$run" "$step" /proc/1/cmdline > /dev/null
head -18 "$base/tmp/query.strace"
