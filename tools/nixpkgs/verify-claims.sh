#!/usr/bin/env bash
# Checks three things the write-up says: which store path of guff's
# closure refers to zlib, how a rebuilt Python wheel differs from the
# cache's, and how a rebuilt perllocal.pod does.
set -u
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
scratch=$(mktemp -d /dev/shm/claims.XXXXXX)
trap 'chmod -R u+w "$scratch"; rm -rf "$scratch"' EXIT
cd "$base/ws" || exit 1

echo "== who in guff's closure refers to zlib"
zlib=/nix/store/wgw25d181h0qks7qsfp1bh7c5nd3f0s7-zlib-1.3.2
while IFS=$'\x1f' read -r path holder; do
    hash=${path#/nix/store/}
    hash=${hash%%-*}
    if grep -q "^References:.*${zlib#/nix/store/}" "narinfo/$hash.narinfo" 2> /dev/null; then
        echo "$path"
    fi
done < pkgs/*-guff-0.1.0/closure.txt

echo "== the wheel"
built=$(ls bazel-bin/pkgs/*aiodns-4.0.4/archive_dist.nar)
nix-store --restore "$scratch/dist" < "$built"
store=/nix/store/zrxs0pv27r5vg1kd3g0dm3w1grlkpk76-python3.13-aiodns-4.0.4-dist
nix-store --realise "$store" > /dev/null 2>&1
unzip=$(nix build --no-link --print-out-paths nixpkgs#unzip 2> /dev/null)/bin/unzip
diff <("$unzip" -v "$scratch"/dist/*.whl) <("$unzip" -v "$store"/*.whl) | head -8

echo "== perllocal.pod"
built=$(ls bazel-bin/pkgs/*Text-CharWidth-0.04/archive_out.nar)
nix-store --restore "$scratch/perl" < "$built"
store=/nix/store/7ly7iiq79jcpdm7c9q7wzy8089m5q1zd-perl5.42.3-Text-CharWidth-0.04
nix-store --realise "$store" > /dev/null 2>&1
diff "$scratch"/perl/lib/perl5/5.42.3/x86_64-linux-thread-multi/perllocal.pod "$store"/lib/perl5/5.42.3/x86_64-linux-thread-multi/perllocal.pod
