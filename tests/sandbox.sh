# The sandbox script is what a generated genrule runs, so its staging and
# its check of the results are tested directly. No process is started,
# which needs bubblewrap, so each case is a step with nothing to run: the
# script stages the manifest's inputs and then compares the results it was
# told to expect with what is on disk.
set -eu
compiler=$1
data=$2
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

# Emitting the example yields the script.
"$compiler" import "$data/example" > "$scratch/graph.mlir"
"$compiler" emit "$data/example" "$scratch/graph.mlir" "$scratch/workspace"
script=$scratch/workspace/sandbox.sh

# A manifest line: tag, kind, mode, mtime, path, content and artifact,
# separated by the unit separator.
line() {
    printf '%s\x1f%s\x1f%s\x1f%s\x1f%s\x1f%s\x1f%s\n' "$@"
}
bundle=cb94740ad765d0c3cb3742959e2f7556d4c7adb712660bfbacb1ba2c6f50e671
step() {
    # Runs the step named $1 with the recording's blobs as its inputs.
    "$script" --name "$1" --in "$scratch" --in "$data/example" --out "$scratch/out"
}
mkdir -p "$scratch/actions"

# A step that leaves what it was told to expect copies the file out under
# its artifact name and writes its stamp.
{
    line in dir 700 1 /build '' ''
    line in file 644 1 /build/bundle "$bundle" "blobs/$bundle"
    line out file 644 1 /build/bundle "$bundle" same/build/bundle
    line out dir 700 1 /build '' ''
} > "$scratch/actions/same.manifest"
step same
test "$(sha256sum < "$scratch/out/same/build/bundle" | cut -d' ' -f1)" = "$bundle"
test -e "$scratch/out/same.ok"

# A step whose results are all directories still leaves a directory named
# after itself, which is the one output a Buck2 rule declares.
{
    line in dir 700 1 /build '' ''
    line out dir 700 1 /build '' ''
} > "$scratch/actions/bare.manifest"
step bare
test -d "$scratch/out/bare"

# A result with other bytes, another mode or no file at all fails the step,
# and the report names each one the way `learn` reads it.
{
    line in dir 700 1 /build '' ''
    line in file 644 1 /build/bundle "$bundle" "blobs/$bundle"
    line out file 644 1 /build/bundle 0000 other/build/bundle
    line out file 644 1 /build/absent "$bundle" other/build/absent
    line out dir 755 1 /build '' ''
} > "$scratch/actions/other.manifest"
if step other 2> "$scratch/error"; then
    exit 1
fi
grep -q '^other did not reproduce the recording:$' "$scratch/error"
grep -q "^  differs /build/bundle: got file 644 $bundle, recorded file 644 0000$" "$scratch/error"
grep -q '^  missing /build/absent$' "$scratch/error"
grep -q '^  differs /build: got dir 700 , recorded dir 755 $' "$scratch/error"
test ! -e "$scratch/out/other.ok"

# A symlink is staged from its entry alone and packed as a symlink.
{
    line in dir 755 1 /out '' ''
    line in link 0 1 /out/link bundle ''
    line in file 644 1 /out/bundle "$bundle" "blobs/$bundle"
} > "$scratch/actions/tree.manifest"
"$script" --name tree --in "$scratch" --in "$data/example" --out "$scratch/out" pack /out
"$compiler" verify "$scratch/out/tree.nar" "$scratch/out/tree.nar" > /dev/null
grep -q symlink "$scratch/out/tree.nar"
