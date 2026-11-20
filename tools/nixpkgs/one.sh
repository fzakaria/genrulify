#!/usr/bin/env bash
# Records one nixpkgs derivation in Rewind and derives its checked graph,
# timing each phase. A dev harness for the whole-nixpkgs run on leviathan:
# it appends one JSON line per derivation to results.jsonl and leaves the
# recording in work/KEY for the emit step.
#
# usage: one.sh ATTR DERIVATION.drv
#
# NIXPKGS_BASE      the batch directory, ~/genrulify-nixpkgs by default
# RECORD_TIMEOUT    seconds a build may run in Rewind
# VM_MEMORIES       MiB of memory to give the VM, each tried in turn until
#                   the build succeeds. Rewind allows at most 3072
# MAX_LOCAL_BUILDS  inputs that may be built here when no cache has them
# SNAPSHOT_TIMEOUT  seconds the snapshots, and then the replays, may take
# MAX_EXECS         commands a build may run and still be cut at every
#                   command. A build that ran more is cut only at the
#                   commands the builder itself ran.
# GENRULIFY_JOBS    threads one conversion may use
set -u
attr=$1
drv=$2
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
key=$(basename "$drv" .drv)
work=$base/work/$key
log=$base/logs/pkg/$key.log
record_timeout=${RECORD_TIMEOUT:-1800}
max_local_builds=${MAX_LOCAL_BUILDS:-6}
vm_memories=${VM_MEMORIES:-1024 3072}
max_execs=${MAX_EXECS:-2500}
snapshot_timeout=${SNAPSHOT_TIMEOUT:-2400}

# The status `timeout` exits with when the deadline passed.
TIMED_OUT=124

# What Rewind says of a derivation it will not build because the
# derivation fetches.
FIXED_OUTPUT_MARK='is a fixed-output derivation'

# How a derivation says, in its .drv file, that it is to be built where
# it is needed and not fetched: as an environment variable, or as a
# field of its structured attributes, which the file holds as quoted JSON.
LOCAL_BUILD_MARK='preferLocalBuild(\\":true|","1")'

# Settings can be changed while lanes run: each job reads this file, which
# may assign any of the settings above.
if [ -f "$base/config.sh" ]; then
    # shellcheck source=/dev/null
    . "$base/config.sh"
fi
export GENRULIFY_JOBS=${GENRULIFY_JOBS:-8}
export PATH=$base/roots/rewind/bin:$base/roots/jq/bin:$PATH
export TMPDIR=$base/tmp
compiler=$base/roots/genrulify/bin/genrulify
mkdir -p "$base/work" "$base/logs/pkg" "$base/locks" "$base/recordings" "$base/roots/inputs/$key" "$TMPDIR"

# Lanes share one list of jobs. A derivation another lane has taken, now
# or in an earlier run, is left alone.
if ! mkdir "$base/locks/$key" 2> /dev/null; then
    exit 0
fi

# Milliseconds since the epoch, for phase timings. A phase's time is
# kept in seconds with one decimal.
MILLISECONDS_PER_SECOND=1000
MILLISECONDS_PER_TENTH=100
now() { date +%s%3N; }
phase_start=$(now)
declare -A seconds
lap() {
    local end elapsed
    end=$(now)
    elapsed=$((end - phase_start))
    seconds[$1]=$((elapsed / MILLISECONDS_PER_SECOND)).$((elapsed % MILLISECONDS_PER_SECOND / MILLISECONDS_PER_TENTH))
    phase_start=$end
}

# Writes the result line and ends the job. Everything the line says is
# read back out of files, so a phase that never ran leaves a null.
finish() {
    local verdict=$1 run_json=$work/run.json check_json=$work/recording/check.json
    jq -cn --arg attr "$attr" --arg drv "$drv" --arg verdict "$verdict" \
        --arg inputs "${seconds[inputs]:-}" --arg rewind "${seconds[rewind]:-}" \
        --arg events "${seconds[events]:-}" --arg record "${seconds[record]:-}" --arg check "${seconds[check]:-}" \
        --arg execs "${execs:-}" --arg granularity "${granularity:-}" --arg memory "${vm_memory:-}" \
        --arg summary "$(grep -m1 'process groups:' "$log" 2> /dev/null)" \
        --arg files "$(grep -m1 ' files (' "$log" 2> /dev/null)" \
        --slurpfile run <(cat "$run_json" 2> /dev/null || echo null) \
        '{attr: $attr, drv: $drv, verdict: $verdict,
          seconds: {inputs: $inputs, rewind: $rewind, events: $events, record: $record, check: $check},
          execs: $execs, granularity: $granularity, memory: $memory, summary: $summary, files: $files,
          steps: $run[0].steps, ending: $run[0].ending,
          outputs: [$run[0].outputs[]? | {path, hash, cache: ([.comparisons[]? | select(.source == "https://cache.nixos.org") | .verdict] | first)}]}' \
        > "$work.result" 2>> "$log"
    flock "$base/results.lock" sh -c "cat '$work.result' >> '$base/results.jsonl'"
    rm -f "$work.result"

    # A run is only needed until its snapshots are taken, and the inputs
    # until the groups have been replayed.
    if [ -n "${run:-}" ]; then
        rewind remove "$run" > /dev/null 2>&1
    fi
    rm -rf "$base/roots/inputs/$key"

    # The recording is kept packed for the emit step. Its snapshots are
    # mostly the same listing again and again, which a long match window
    # folds away.
    if [ "$verdict" = ok ]; then
        tar -C "$work" -cf - recording | zstd -q -T2 -3 --long=27 -o "$base/recordings/$key.tar.zst" 2>> "$log"
    fi
    chmod -R u+w "$work" 2> /dev/null
    rm -rf "$work"
    exit 0
}

rm -rf "$work"
mkdir -p "$work"
: > "$log"

# The build's inputs have to be in the store, and stay there: this
# machine collects garbage while a run is going. Caches hold nearly all
# of them. A wrapper or a hook that nixpkgs marks to be built where it is
# needed is built here. A derivation with an input of any other kind
# that no cache has is skipped, as is one that needs many such builds,
# so that no lane ends up compiling somebody's dependency.
mapfile -t inputs < <(nix-store --query --references "$drv")
mapfile -t to_build < <(nix-store --realise --dry-run "${inputs[@]}" 2>&1 |
    sed -n '/will be built:/,/will be fetched/p' | grep -o '/nix/store/.*\.drv$')
if [ "${#to_build[@]}" -gt "$max_local_builds" ]; then
    echo "needs ${#to_build[@]} local builds" >> "$log"
    lap inputs
    finish no-inputs
fi
for local_drv in "${to_build[@]}"; do
    if ! grep -Eq "$LOCAL_BUILD_MARK" "$local_drv"; then
        echo "needs a build of $local_drv" >> "$log"
        lap inputs
        finish no-inputs
    fi
done
if ! nix-store --realise --add-root "$base/roots/inputs/$key/r" --indirect "${inputs[@]}" >> "$log" 2>&1; then
    lap inputs
    finish no-inputs
fi
lap inputs

# Build the derivation in the deterministic VM. /build is in the VM's
# memory, and a VM with more memory makes every later query cost more,
# so a build gets the small VM first and the large one only when it
# failed in the small one.
for vm_memory in $vm_memories; do
    if [ -n "${run:-}" ]; then
        rewind remove "$run" > /dev/null 2>&1
    fi
    rewind nix "$drv" --quiet --json --timeout "$record_timeout" --mem "$vm_memory" > "$work/run.json" 2>> "$log"
    if ! run=$(jq -er .id "$work/run.json" 2>> "$log"); then
        lap rewind

        # A fetcher needs the network, which a run does not have. Its
        # output is an input of the builds that use it.
        if grep -q "$FIXED_OUTPUT_MARK" "$log"; then
            finish fixed-output
        fi
        finish rewind-failed
    fi
    if [ "$(jq -r .ending "$work/run.json")" = exited:0 ]; then
        break
    fi
done
lap rewind
if [ "$(jq -r .ending "$work/run.json")" != exited:0 ]; then
    finish "build-$(jq -r .ending "$work/run.json")"
fi

# Count the commands before paying for a snapshot of each. Inspecting a
# command costs several Rewind queries, so a build that ran many is cut
# only at the commands the builder itself ran, which leaves a `make` as
# one rule.
execs=$(rewind events --json "$run" 2>> "$log" | grep -c '"type":"exec"')
lap events
granularity=fine
coarse=()
if [ "$execs" -gt "$max_execs" ]; then
    granularity=coarse
    coarse=(--coarse)
fi

# Cut the run into commands and snapshot around each.
# A build with a long stretch of computation makes every snapshot after
# the stretch replay it, so the phase has a deadline of its own.
timeout "$snapshot_timeout" "$compiler" record "${coarse[@]}" "$run" "$drv" "$work/recording" >> "$log" 2>&1
status=$?
if [ "$status" = "$TIMED_OUT" ]; then
    lap record
    finish record-timeout
fi
if [ "$status" != 0 ]; then
    lap record
    finish record-failed
fi
lap record

# Replay every live group against the recording.
timeout "$snapshot_timeout" "$compiler" check "$work/recording" >> "$log" 2>&1
status=$?
if [ "$status" = "$TIMED_OUT" ]; then
    lap check
    finish check-timeout
fi
if [ "$status" != 0 ]; then
    lap check
    finish check-failed
fi
lap check
finish ok
