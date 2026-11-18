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

# The rest starts processes, which needs bubblewrap and user namespaces.
# A build sandbox may have neither, so try one command and stop here when
# it cannot run. The commands are the coreutils the example was recorded
# with, whose store closure is the example's.
coreutils=$(grep -m1 -- '-coreutils-' "$scratch/workspace/closure.txt")/bin
if [ ! -x "$coreutils/true" ]; then
    exit 0
fi
: > "$scratch/actions/probe.manifest"
if ! "$script" --name probe --in "$scratch/workspace" --in "$scratch" --out "$scratch/out" \
    run --env 0 --status 0 --argc 1 -- "$coreutils/true" 2> /dev/null; then
    echo "skipping the cases that start processes" >&2
    exit 0
fi

# Runs the step named $1 with the workspace's closure and environment.
start() {
    name=$1
    shift
    "$script" --name "$name" --in "$scratch/workspace" --in "$scratch" --out "$scratch/out" --timeout 20 "$@"
}

# A chain of pipes ends when its last reader does. `yes` never stops by
# itself: head exits after one line, cat is then killed writing to a pipe
# nobody reads, and yes after cat. Whichever pipe the script happens to
# look after first, no helper may keep the other pipe's read end open, or
# cat would never be told. Both orders of the two pipe ids are tried.
SIGPIPE_STATUS=141
for ids in "1 2" "2 1"; do
    set -- $ids
    : > "$scratch/actions/chain.manifest"
    start chain --pipe "$1:0:1:1" --pipe "$2:1:2:1" \
        run --env 0 --fd "1=pipe-w:$1" --status "$SIGPIPE_STATUS" --argc 1 -- "$coreutils/yes" \
        run --env 0 --fd "0=pipe-r:$1" --fd "1=pipe-w:$2" --status "$SIGPIPE_STATUS" --argc 1 -- "$coreutils/cat" \
        run --env 0 --fd "0=pipe-r:$2" --fd 1=null --status 0 --argc 3 -- "$coreutils/head" -n 1
done

# A descriptor sent to the script's standard error gets that, whatever
# order the descriptors are listed in. Here descriptor 2 is a file and is
# listed before descriptor 1, which is where echo writes. Nothing may
# reach the file.
{
    line in dir 700 1 /build '' ''
    line out file 644 1 /build/log e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 order/build/log
} > "$scratch/actions/order.manifest"
start order run --cwd /build --env 0 --fd 2=file:w:/build/log --fd 1=err --status 0 --argc 2 -- \
    "$coreutils/echo" written 2> "$scratch/order.err"
grep -q '^written$' "$scratch/order.err"
