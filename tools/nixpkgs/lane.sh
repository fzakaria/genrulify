#!/usr/bin/env bash
# One lane of the whole-nixpkgs run: takes jobs off a shared list, one
# "ATTR DERIVATION" pair per line, until none is left or a stop file
# appears.
#
# usage: lane.sh JOBS-FILE
set -u
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
here=$(dirname "$0")
while read -r attr drv; do
    if [ -e "$base/stop" ]; then
        break
    fi
    "$here/one.sh" "$attr" "$drv" < /dev/null
done < "$1"
