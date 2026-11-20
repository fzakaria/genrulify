#!/usr/bin/env bash
# Runs the commands a person would type to make a shared workspace of
# two packages, and keeps what they print: the transcript for the docs.
#
# usage: demo.sh
set -u
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
demo=$base/demo
export PATH=$base/roots/genrulify-demo/bin:$base/roots/rewind/bin:$base/roots/bazel/bin:$PATH
export GENRULIFY_JOBS=${GENRULIFY_JOBS:-16}
nixpkgs=github:NixOS/nixpkgs/b4fd65b198c599cbe814fcb9f42d25d021595ec9
chmod -R u+w "$demo" 2> /dev/null
rm -rf "$demo"
mkdir -p "$demo"
cd "$demo" || exit 1

# Prints a command the way a console block shows one, then runs it.
show() {
    echo "\$ $*"
    "$@"
}
{
    show genrulify convert --skip-idle --into nixpkgs.bazel "$nixpkgs#zlib" zlib
    show genrulify convert --skip-idle --into nixpkgs.bazel "$nixpkgs#guff" guff
    show genrulify link nixpkgs.bazel
    cd nixpkgs.bazel || exit 1
    show bazel --output_user_root="$demo/bazel-root" build //pkgs/...
    show sha256sum bazel-bin/pkgs/*/archive*.nar
    bazel --output_user_root="$demo/bazel-root" shutdown
} > "$demo/transcript.txt" 2>&1
echo "demo exit $?" >> "$demo/transcript.txt"
