#!/usr/bin/env bash
# Builds one derivation in Rewind and keeps what the build printed, to
# see why a build failed there.
#
# usage: diag.sh NAME DERIVATION.drv [REWIND-OPTION...]
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
name=$1
drv=$2
shift 2
"$base/roots/rewind/bin/rewind" nix "$drv" --timeout 900 "$@" > "$base/logs/diag-$name.log" 2>&1
echo "exit $?" >> "$base/logs/diag-$name.log"
