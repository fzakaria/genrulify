#!/usr/bin/env bash
# Stops every lane of the whole-nixpkgs run and what the lanes started.
# The patterns are in this file so that the shell running the file is not
# matched.
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
touch "$base/stop"
pkill -u "$(id -u)" -f 'tools/lane[.]sh'
pkill -u "$(id -u)" -f 'tools/one[.]sh'
pkill -u "$(id -u)" -f 'roots/genrulify/bin/genrulify'
pkill -u "$(id -u)" -f 'rewind .*--progress off'
pkill -u "$(id -u)" -f 'rewind nix /nix/store'
