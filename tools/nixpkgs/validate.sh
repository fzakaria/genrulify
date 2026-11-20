#!/usr/bin/env bash
# Builds, with Bazel in the shared workspace, every package that has not
# been built there yet, and records for each whether all its archives
# came out. An archive rule fails unless its NAR has the digest the
# recording had, so a package that builds is a package that reproduced.
#
# usage: validate.sh
#
# VALIDATE_JOBS     actions Bazel may run at once
# VALIDATE_BATCH    packages to build in one Bazel invocation
set -u
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
export PATH=$base/roots/jq/bin:$PATH
jobs=${VALIDATE_JOBS:-24}
batch=${VALIDATE_BATCH:-200}
mkdir -p "$base/validated" "$base/logs/validate"

# The packages emitted and not yet built, a batch of them.
mapfile -t keys < <(cd "$base/emitted" && for key in *; do
    [ -e "$base/validated/$key" ] || echo "$key"
done | head -n "$batch")
if [ "${#keys[@]}" -eq 0 ]; then
    exit 0
fi

# A package's label comes from the label of its main archive.
targets=()
declare -A key_of
for key in "${keys[@]}"; do
    label=$(sed 's/:.*$/:outputs/' "$base/emitted/$key")
    targets+=("$label")
    key_of[$label]=$key
done

# Bazel says how each target ended in its build events.
stamp=$(date +%s)
events=$base/logs/validate/$stamp.events.json
"$base/tools/bazel.sh" build --keep_going --jobs="$jobs" \
    --build_event_json_file="$events" "${targets[@]}" > "$base/logs/validate/$stamp.log" 2>&1
while IFS=$'\t' read -r label success; do
    key=${key_of[$label]:-}
    [ -n "$key" ] || continue
    echo "$success" > "$base/validated/$key"
done < <(jq -r 'select(.id.targetCompleted != null) | [.id.targetCompleted.label, (.completed.success // false)] | @tsv' "$events")

# A target Bazel never got to analyze has no event, and counts as failed.
for key in "${keys[@]}"; do
    [ -e "$base/validated/$key" ] || echo false > "$base/validated/$key"
done
echo "$(grep -lx true "$base"/validated/* | wc -l) built, $(grep -lx false "$base"/validated/* | wc -l) failed"
