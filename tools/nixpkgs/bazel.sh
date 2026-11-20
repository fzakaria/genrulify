#!/usr/bin/env bash
# Runs Bazel in the shared workspace of the whole-nixpkgs run, with its
# output kept under the batch directory.
#
# usage: bazel.sh BAZEL-ARGUMENT...
set -u
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
cd "$base/ws" || exit 1
exec "$base/roots/bazel/bin/bazel" --output_user_root="$base/bazel-root" "$@"
