# A command that leaves /build and the outputs as it found them writes
# nothing a later command can read, and `record --skip-idle` spends no
# Rewind query on it beyond one snapshot. Rewind needs KVM, so the test
# covers the decision, which `idle` makes for a finished recording the
# way `record` makes it for a run: commands whose lifetimes overlap are
# one stretch, and a stretch is idle when the snapshot at its start is
# the snapshot at the start of the next one, or at the end of the build
# for the last.
set -eu
compiler=$1
data=$2
scratch=$(mktemp -d)
trap 'chmod -R u+w "$scratch"; rm -rf "$scratch"' EXIT

# Each dd of the example writes a file, so none is idle.
test -z "$("$compiler" idle "$data/example")"

# A copy in which the second dd changed nothing: the snapshots after it,
# and before the third, are the one before it.
cp -r "$data/example" "$scratch/quiet"
chmod -R u+w "$scratch/quiet"
cp "$scratch/quiet/states/764.json" "$scratch/quiet/states/778.json"
cp "$scratch/quiet/states/764.json" "$scratch/quiet/states/782.json"
test "$("$compiler" idle "$scratch/quiet")" = 764

# A command that wrote something the builder then undid, before the next
# command began, also left nothing: only the snapshots at the starts of
# stretches are compared.
cp -r "$data/example" "$scratch/undone"
chmod -R u+w "$scratch/undone"
cp "$scratch/undone/states/764.json" "$scratch/undone/states/782.json"
test "$("$compiler" idle "$scratch/undone")" = 764

# The last command is compared with the end of the build.
cp -r "$data/example" "$scratch/last"
chmod -R u+w "$scratch/last"
cp "$scratch/last/states/782.json" "$scratch/last/states/796.json"
test "$("$compiler" idle "$scratch/last")" = 782

# Commands that ran at the same time are one stretch: with the second dd
# still running when the third began, the two are idle together or not
# at all. Here the second alone changed nothing, and the stretch did.
cp -r "$scratch/quiet" "$scratch/overlap"
sed -i 's/"exit": 778/"exit": 790/' "$scratch/overlap/recording.json"
grep -q '"exit": 790' "$scratch/overlap/recording.json"
test -z "$("$compiler" idle "$scratch/overlap")"

# Skipping commands is for a recorded build. A project converted in place
# runs commands that write nothing, to see that they still end the same
# way after an edit, so the two options do not go together.
if "$compiler" record --skip-idle --source "$scratch" RUN /nix/store/x.drv "$scratch/out" 2> "$scratch/error"; then
    exit 1
fi
grep -q 'skip-idle' "$scratch/error"
"$compiler" 2>&1 | grep -q 'genrulify record \[--coarse\] \[--skip-idle\] \[--source PROJECT\] RUN DERIVATION.drv RECORDING \[OPEN\]'
