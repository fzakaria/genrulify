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

# The same with an eighth field, the origin of an entry to stage: the rule
# whose stamp says what the entry is, or @checkout for a project's file.
origin_line() {
    printf '%s\x1f%s\x1f%s\x1f%s\x1f%s\x1f%s\x1f%s\x1f%s\n' "$@"
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

# A step can be given thousands of files, so staging does not start a
# process for each. A file whose artifact is named after its producer and
# then its guest path, as the files of a workspace are, is copied together
# with the producer's other files. Any other file is copied on its own.
# Either way each file has to arrive with its bytes and its recorded mode,
# which the step's results check here: two files of the producer `maker`,
# a file from the blobs, a symlink, and a directory whose mode keeps its
# owner from writing into it, which can only be set once the file below
# it is in place.
mkdir -p "$scratch/maker/build/sub" "$scratch/maker/build/shut"
for file in a.txt sub/b.txt shut/x.txt; do
    cp "$data/example/blobs/$bundle" "$scratch/maker/build/$file"
done
{
    line in dir 700 1 /build '' ''
    line in dir 750 1 /build/sub '' ''
    line in dir 500 1 /build/shut '' ''
    line in file 600 1 /build/a.txt "$bundle" maker/build/a.txt
    line in file 755 1 /build/sub/b.txt "$bundle" maker/build/sub/b.txt
    line in file 444 1 /build/shut/x.txt "$bundle" maker/build/shut/x.txt
    line in file 640 1 /build/c.txt "$bundle" "blobs/$bundle"
    line in link 0 1 /build/sub/link ../a.txt ''
    line out file 600 1 /build/a.txt "$bundle" batch/build/a.txt
    line out file 755 1 /build/sub/b.txt "$bundle" batch/build/sub/b.txt
    line out file 444 1 /build/shut/x.txt "$bundle" batch/build/shut/x.txt
    line out file 640 1 /build/c.txt "$bundle" batch/build/c.txt
    line out link 0 1 /build/sub/link ../a.txt ''
    line out dir 750 1 /build/sub '' ''
    line out dir 500 1 /build/shut '' ''
} > "$scratch/actions/batch.manifest"
step batch
for file in a.txt sub/b.txt shut/x.txt c.txt; do
    cmp "$scratch/out/batch/build/$file" "$data/example/blobs/$bundle"
done

# A step whose results are all directories still writes its stamp, which
# is then the one file the build system has of it.
{
    line in dir 700 1 /build '' ''
    line out dir 700 1 /build '' ''
} > "$scratch/actions/bare.manifest"
rm -rf "$scratch/out"
step bare
test -e "$scratch/out/bare.ok"

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

# A project's rules run with --trust: a result is whatever the command
# left, as long as it is there and is the kind of thing that was recorded.
# Other bytes and another mode pass, and the file is handed on. A missing
# result still fails, and the report says what to do about it.
{
    line in dir 700 1 /build '' ''
    line in file 644 1 /build/bundle "$bundle" "blobs/$bundle"
    line out file 600 1 /build/bundle 0000 trusted/build/bundle
} > "$scratch/actions/trusted.manifest"
"$script" --name trusted --trust --in "$scratch" --in "$data/example" --out "$scratch/out"
test "$(sha256sum < "$scratch/out/trusted/build/bundle" | cut -d' ' -f1)" = "$bundle"
{
    line in dir 700 1 /build '' ''
    line out file 644 1 /build/absent 0000 gone/build/absent
    line out dir 755 1 /build '' ''
} > "$scratch/actions/gone.manifest"
if "$script" --name gone --trust --in "$scratch" --in "$data/example" --out "$scratch/out" 2> "$scratch/error"; then
    exit 1
fi
grep -q '^  missing /build/absent$' "$scratch/error"
grep -q 'convert the project again' "$scratch/error"

# A project's rule is not held to the recording, so the report must not
# say that the rule failed to reproduce it.
grep -q '^gone did not leave what the recorded build left:$' "$scratch/error"
if grep -q 'did not reproduce the recording' "$scratch/error"; then
    exit 1
fi
if grep -q 'differs /build:' "$scratch/error"; then
    exit 1
fi

# A trusted step says in its stamp what it left: the mode of each file
# and directory and the target of each symlink, and no digest, so that an
# edit to a file's text does not change the stamp. Here the step is given
# a file with mode 644 and a symlink to `bundle`, where the recording had
# mode 600 and a symlink to `elsewhere`.
{
    line in dir 700 1 /build '' ''
    line in file 644 1 /build/bundle "$bundle" "blobs/$bundle"
    line in link 0 1 /build/link bundle ''
    line out file 600 1 /build/bundle 0000 seen/build/bundle
    line out link 0 1 /build/link elsewhere ''
    line out dir 755 1 /build '' ''
} > "$scratch/actions/seen.manifest"
"$script" --name seen --trust --in "$scratch" --in "$data/example" --out "$scratch/out"
printf 'file\x1f644\x1f/build/bundle\x1f\nlink\x1f0\x1f/build/link\x1fbundle\ndir\x1f700\x1f/build\x1f\n' > "$scratch/expected.ok"
cmp "$scratch/out/seen.ok" "$scratch/expected.ok"

# A step given an entry a trusted rule made stages what that rule's stamp
# says and not what was recorded. The recording had a file nobody can
# execute and a symlink to `old`. The stamp of the rule `maker` says the
# file is executable and the symlink points to `new`, and the archive
# packed from them has both. An entry the stamp does not mention, and an
# entry of a rule that has no stamp here, are staged as recorded.
printf 'file\x1f755\x1f/out/tool\x1f\nlink\x1f0\x1f/out/link\x1fnew\n' > "$scratch/maker.ok"
{
    line in dir 755 1 /out '' ''
    origin_line in file 644 1 /out/tool "$bundle" "blobs/$bundle" maker
    origin_line in link 0 1 /out/link old '' maker
    origin_line in file 644 1 /out/plain "$bundle" "blobs/$bundle" maker
    origin_line in file 644 1 /out/other "$bundle" "blobs/$bundle" absent
} > "$scratch/actions/handed.manifest"
"$script" --name handed --in "$scratch" --in "$data/example" --out "$scratch/out" pack /out
test "$(grep -a -o executable "$scratch/out/handed.nar" | wc -l)" = 1
grep -a -q new "$scratch/out/handed.nar"
if grep -a -q old "$scratch/out/handed.nar"; then
    exit 1
fi

# A file of the project keeps the executable bit the project's file has
# now, whatever the recording had: a recorded 444 becomes 555 for a file
# that is executable, and a recorded 755 becomes 644 for one that is not.
mkdir "$scratch/checkout"
cp "$data/example/blobs/$bundle" "$scratch/checkout/runs"
cp "$data/example/blobs/$bundle" "$scratch/checkout/rests"
chmod 755 "$scratch/checkout/runs"
chmod 644 "$scratch/checkout/rests"
{
    line in dir 755 1 /build '' ''
    origin_line in file 444 1 /build/runs "$bundle" checkout/runs @checkout
    origin_line in file 755 1 /build/rests "$bundle" checkout/rests @checkout
    line out file 444 1 /build/runs "$bundle" modes/build/runs
    line out file 755 1 /build/rests "$bundle" modes/build/rests
} > "$scratch/actions/modes.manifest"
"$script" --name modes --trust --in "$scratch" --out "$scratch/out"
grep -q "^file.555./build/runs.\$" "$scratch/out/modes.ok"
grep -q "^file.644./build/rests.\$" "$scratch/out/modes.ok"

# A trusted step that leaves an entry under an output path which the
# recorded command did not write fails and names the entry: no rule would
# carry it into the package. Staging a file makes the directories above
# it, and here those have no line of their own, which stands for a
# command that made them. An entry the recorded command wrote and nothing
# reads is `unused`, and is no problem. Outside the output paths nothing
# is looked at.
mkdir "$scratch/project-files"
printf '/out\n' > "$scratch/project-files/outputs.txt"
{
    line in dir 755 1 /out '' ''
    line in file 644 1 /out/extra/file "$bundle" "blobs/$bundle"
    line in file 644 1 /build/extra/file "$bundle" "blobs/$bundle"
    line out dir 755 1 /out '' ''
} > "$scratch/actions/more.manifest"
more() {
    "$script" --name more --trust --in "$scratch/project-files" --in "$scratch" --in "$data/example" --out "$scratch/out"
}
if more 2> "$scratch/error"; then
    exit 1
fi
grep -q '^more did not leave what the recorded build left:$' "$scratch/error"
grep -q '^  unrecorded /out/extra$' "$scratch/error"
grep -q 'convert the project again' "$scratch/error"
if grep -q 'unrecorded /build' "$scratch/error"; then
    exit 1
fi
line unused dir 755 1 /out/extra '' '' >> "$scratch/actions/more.manifest"
more

# A symlink is staged from its entry alone and packed as a symlink.
{
    line in dir 755 1 /out '' ''
    line in link 0 1 /out/link bundle ''
    line in file 644 1 /out/bundle "$bundle" "blobs/$bundle"
} > "$scratch/actions/tree.manifest"
"$script" --name tree --in "$scratch" --in "$data/example" --out "$scratch/out" pack /out
"$compiler" verify "$scratch/out/tree.nar" "$scratch/out/tree.nar" > /dev/null
grep -q symlink "$scratch/out/tree.nar"

# Given the digest an archive must have, packing checks it. The right
# digest passes. Another one fails, says both, and leaves no archive for
# a later rule to pick up.
packed=$(sha256sum < "$scratch/out/tree.nar" | cut -d' ' -f1)
rm "$scratch/out/tree.nar"
"$script" --name tree --in "$scratch" --in "$data/example" --out "$scratch/out" pack /out "$packed"
test -e "$scratch/out/tree.nar"
rm "$scratch/out/tree.nar"
if "$script" --name tree --in "$scratch" --in "$data/example" --out "$scratch/out" pack /out "$bundle" 2> "$scratch/error"; then
    exit 1
fi
grep -q '^tree did not reproduce the recording:$' "$scratch/error"
grep -q "^  the archive of /out is sha256:$packed, recorded sha256:$bundle\$" "$scratch/error"
test ! -e "$scratch/out/tree.nar"

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
