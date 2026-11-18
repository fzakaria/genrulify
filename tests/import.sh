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

# A process whose standard input is a pipe only the builder wrote, as a
# here-string is, can be replayed only if the recording captured the bytes
# that were waiting in the pipe. Give the append such a descriptor: without
# captured bytes the group is demoted, and with them it is a splice.exec.
with_pipe() {
    # $1 is the JSON to add to the descriptor, $2 the recording to write.
    cp -r "$data/example" "$2"
    chmod -R u+w "$2"

    # The append is the process with "step": 764. Its descriptor list opens
    # on the nearest "fds" line above that one.
    step=$(grep -n '"step": 764' "$data/example/recording.json" | cut -d: -f1)
    fds=$(head -n "$step" "$data/example/recording.json" | grep -n '"fds": \[' | tail -n 1 | cut -d: -f1)
    awk -v at="$fds" -v extra="$1" '
        { print }
        NR == at {
            print "{\"fd\": 0, \"target\": \"pipe:[9]\", \"mode\": 256, \"flags\": 0, \"position\": 0" extra "},"
        }' "$data/example/recording.json" > "$2/recording.json"
}
with_pipe '' "$scratch/unfed"
"$compiler" import "$scratch/unfed" > "$scratch/unfed.mlir"
grep -q 'splice.literal "dd" at 764 demoted' "$scratch/unfed.mlir"
with_pipe ', "input": "68690a"' "$scratch/fed"
"$compiler" import "$scratch/fed" > "$scratch/fed.mlir"
grep -q 'splice.exec "dd" at 764' "$scratch/fed.mlir"
