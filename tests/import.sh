# Import the captured script, preserve both versions and fold its two copies.
set -eu
compiler=$1
data=$2
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
"$compiler" import "$data/derivation.json" "$data/events.jsonl" > "$scratch/graph.mlir"
test "$(grep -c splice.copy "$scratch/graph.mlir")" = 2
grep -q 'splice.append .*version = 2' "$scratch/graph.mlir"
"$compiler" opt "$scratch/graph.mlir" > "$scratch/opt.mlir"
test "$(grep -c splice.copy "$scratch/opt.mlir")" = 0
grep -q splice.append "$scratch/opt.mlir"

# Missing execs, write opens and child exits must each invalidate the import.
for step in 764 746 778; do
    grep -v "\"step\":$step," "$data/events.jsonl" > "$scratch/events"
    if "$compiler" import "$data/derivation.json" "$scratch/events" >/dev/null; then
        exit 1
    fi
done
