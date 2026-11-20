#!/usr/bin/env bash
# Records one derivation with a `rewind` on PATH that logs how long each
# query took, to see where the snapshot phase spends its time.
#
# usage: profile.sh NAME DERIVATION.drv
#
# PROFILE_COMPILER  the genrulify to record with
# RECORD_OPTIONS    options for `genrulify record`, such as --skip-idle
set -u
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
name=$1
drv=$2
out=$base/profile/$name
rm -rf "$out"
mkdir -p "$out/bin"
real=$base/roots/rewind/bin/rewind

# The shim: run the real program, then log seconds and arguments.
cat > "$out/bin/rewind" <<SHIM
#!/usr/bin/env bash
start=\$(date +%s%N)
"$real" "\$@"
status=\$?
echo "\$(( (\$(date +%s%N) - start) / 1000000 )) \$*" >> "$out/queries.log"
exit \$status
SHIM
chmod +x "$out/bin/rewind"
export PATH=$out/bin:$base/roots/jq/bin:$PATH
export GENRULIFY_JOBS=${GENRULIFY_JOBS:-4}
nix-store --query --references "$drv" | xargs nix-store --realise --add-root "$out/roots/r" --indirect > /dev/null 2>&1
run=$("$real" nix "$drv" --quiet --json | jq -r .id)
start=$(date +%s)
compiler=${PROFILE_COMPILER:-$base/roots/genrulify/bin/genrulify}
# shellcheck disable=SC2086
"$compiler" record ${RECORD_OPTIONS:-} "$run" "$drv" "$out/recording" > "$out/record.log" 2>&1
echo "record took $(($(date +%s) - start))s, status $?"
"$compiler" check "$out/recording" 2>&1 | tail -2
"$real" remove "$run" > /dev/null 2>&1

# Milliseconds by kind of query.
awk '{kind = $4; if ($4 == "cat" && $5 == "--archive") kind = "archive"; else if ($2 != "--progress") kind = $2;
      n[kind]++; ms[kind] += $1; if ($1 > max[kind]) max[kind] = $1}
     END {for (k in n) printf "%-10s %6d queries %9.1f s total %7.2f s mean %7.2f s max\n", k, n[k], ms[k] / 1000, ms[k] / n[k] / 1000, max[k] / 1000}' "$out/queries.log"
ls "$out/recording/states" | wc -l
