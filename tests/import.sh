# Importing the example recording gives file SSA. The builder appends to
# /build/bundle in place, so the path has two versions, and the append
# takes the first version as an operand. The test also applies a Literal
# verdict and checks that the group becomes recorded bytes.
set -eu
compiler=$1
data=$2
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
"$compiler" import "$data/example" > "$scratch/graph.mlir"

# Nix's sandbox, three dd groups and one output.
test "$(grep -c 'splice.literal "sandbox"' "$scratch/graph.mlir")" = 1
test "$(grep -c 'splice.exec "dd"' "$scratch/graph.mlir")" = 3
test "$(grep -c 'splice.output "out"' "$scratch/graph.mlir")" = 1

# Two versions of one path, with different contents.
test "$(grep -c '"/build/bundle" file' "$scratch/graph.mlir")" = 2
test "$(grep '"/build/bundle" file' "$scratch/graph.mlir" | sort -u | wc -l)" = 2

# The append at step 764 reads the build directory and the first version.
grep -q 'splice.exec "dd" at 764(%0, %1)' "$scratch/graph.mlir"

# Every group feeds the output, so optimizing removes nothing.
"$compiler" opt "$scratch/graph.mlir" > "$scratch/opt.mlir"
test "$(grep -c 'splice.exec' "$scratch/opt.mlir")" = 3

# A Literal verdict turns the group into a demoted literal with the same
# written entry, and the later groups still consume that entry.
cp -r "$data/example" "$scratch/recording"
chmod -R u+w "$scratch/recording"
echo '{"764": {"rung": "literal"}}' > "$scratch/recording/verdicts.json"
"$compiler" import "$scratch/recording" > "$scratch/demoted.mlir"
grep -q 'splice.literal "dd" at 764 demoted' "$scratch/demoted.mlir"
test "$(grep -c 'splice.exec "dd"' "$scratch/demoted.mlir")" = 2
