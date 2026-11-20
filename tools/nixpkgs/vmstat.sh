#!/usr/bin/env bash
# Prints, inside the VM of the newest finished run, the modification
# times a stdenv hook compares when it picks SOURCE_DATE_EPOCH.
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
R=$base/roots/rewind/bin/rewind
line=$($R ls | grep 'exited:0' | head -1)
run=${line%% *}
steps=$(awk '{print $3}' <<< "$line")
echo "$line"
printf 'stat -c "%%Y %%n" / /build /build/* 2>&1 | head -8; date +%%s; echo SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH\n' |
    timeout 120 "$R" --progress off shell "$run" $((steps * 3 / 4)) 2>&1 | grep -v '^\[rewind\]\|^rewind:' | head -14
