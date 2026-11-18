# Lowering writes one genrule per operation for both build systems. The
# test emits the example recording and checks the rules, their dependency
# edges, the plan files and the literal layer, then checks that emit will
# not write into an existing directory.
set -eu
compiler=$1
data=$2
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
"$compiler" import "$data/example" > "$scratch/graph.mlir"
"$compiler" emit "$data/example" "$scratch/graph.mlir" "$scratch/workspace"
cd "$scratch/workspace"

# Three replay rules and the archive rule, in both files.
for file in BUILD.bazel BUCK; do
    test "$(grep -c '^genrule(' "$file")" = 4
    grep -q 'name = "s764_dd"' "$file"
    grep -q 'name = "archive"' "$file"
done

# The append depends on the first dd's layer and on the sandbox literal.
grep -A1 'name = "s764_dd"' BUILD.bazel | grep -q '":s735_dd"'
grep -A1 'name = "s764_dd"' BUILD.bazel | grep -q '":literals/sandbox_717.layer"'

# Bazel writes to $@ and Buck2 to $OUT, and Buck2 exports its source files.
grep -q 'replay .*\$@' BUILD.bazel
grep -q 'replay .*\$OUT' BUCK
grep -q 'export_file(name = "plans/s764_dd.json"' BUCK

# A plan carries the recorded argv and the entry the group must reproduce.
grep -q 'oflag=append' plans/s764_dd.json
grep -q 'c82437d39e10d7a564830c757b91784a1d110f5665978f672a1b4119c2ef2b5e' plans/s764_dd.json
test -s literals/sandbox_717.layer
test -s platform.layer

# An existing destination is left alone.
if "$compiler" emit "$data/example" "$scratch/graph.mlir" "$scratch/workspace" 2> /dev/null; then
    exit 1
fi

# The description lists every operation and every process group as JSON.
cd "$scratch"
"$compiler" describe "$data/example" "$scratch/graph.mlir" > "$scratch/description.json"
grep -q '"id":"s764_dd"' "$scratch/description.json"
grep -q '"node":"s735_dd"' "$scratch/description.json"
grep -q '"op":"output"' "$scratch/description.json"
grep -q 'oflag=append' "$scratch/description.json"
