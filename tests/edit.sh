# A project is converted so that it can be built again after an edit. The
# test converts the example as a project whose first dd reads a source
# file, runs the rules of the generated BUILD.bazel one after another the
# way a build system would, edits the source and runs them again. The
# archive has to follow the edit. Neither Rewind nor Bazel is needed: a
# rule is one command line, and the rules are written in dependency order.
set -eu
compiler=$1
data=$2
scratch=$(mktemp -d)
trap 'chmod -R u+w "$scratch"; rm -rf "$scratch"' EXIT

# The example's recording, with a source tree that holds the header, and
# with the first dd reading the header from that tree.
source=/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-source
header=cb94740ad765d0c3cb3742959e2f7556d4c7adb712660bfbacb1ba2c6f50e671
cp -r "$data/example" "$scratch/recording"
chmod -R u+w "$scratch/recording"
sed -i "s|^ \"run\":| \"source\": \"$source\",\n \"run\":|" "$scratch/recording/recording.json"
sed -i "0,/\"reads\": \[\],/s||\"reads\": [\"$source/header.txt\"],|" "$scratch/recording/recording.json"
sed -i "s|if=/nix/store/[a-z0-9]*-header.txt|if=$source/header.txt|" "$scratch/recording/recording.json"
cat > "$scratch/recording/states/source.json" <<JSON
[
 {"content": "", "kind": "dir", "mode": 365, "mtime": 1, "path": "$source"},
 {"content": "$header", "kind": "file", "mode": 292, "mtime": 1, "path": "$source/header.txt"}
]
JSON
"$compiler" import "$scratch/recording" > "$scratch/graph.mlir"
"$compiler" opt "$scratch/graph.mlir" > "$scratch/opt.mlir"
mkdir "$scratch/project"
cp "$data/example/blobs/$header" "$scratch/project/header.txt"
"$compiler" emit "$scratch/recording" "$scratch/opt.mlir" "$scratch/project"
cd "$scratch/project"

# Starting a process needs bubblewrap and user namespaces, and the store
# paths the example was recorded with. Without them there is nothing more
# this test can do.
coreutils=$(grep -m1 -- '-coreutils-' genrulify/closure.txt)/bin
if [ ! -x "$coreutils/true" ]; then
    exit 0
fi
mkdir -p "$scratch/probe/actions"
: > "$scratch/probe/actions/probe.manifest"
if ! genrulify/sandbox.sh --name probe --in genrulify --in "$scratch/probe" --out "$scratch/probe-out" \
    run --env 0 --status 0 --argc 1 -- "$coreutils/true" 2> /dev/null; then
    echo "skipping the cases that start processes" >&2
    exit 0
fi

# Runs every rule of BUILD.bazel in the order the rules are written, with
# the directory $1 for all their outputs. A rule's command is the lines of
# its `cmd`, joined.
build() {
    awk '
        /^    cmd = \[$/ { collecting = 1; command = ""; next }
        collecting && /^    \],$/ { print command; collecting = 0; next }
        collecting {
            sub(/^ *"/, ""); sub(/",$/, "")
            gsub(/\\"/, "\""); gsub(/\\\\/, "\\")
            command = command " " $0
        }
    ' BUILD.bazel > "$scratch/commands"
    while IFS= read -r command; do
        command=${command//'$(RULEDIR)'/$1}
        bash -c "$command"
    done < "$scratch/commands"
}

# The first build archives the recorded header.
old=$(head -c 9 header.txt)
build "$scratch/first"
grep -q -a -F "$old" "$scratch/first/archive.nar"

# After an edit every dd runs again on what the one before it left, so the
# archive holds the new header and no longer holds the old one.
printf 'an edited header\n' > header.txt
build "$scratch/second"
grep -q -a 'an edited header' "$scratch/second/archive.nar"
if grep -q -a -F "$old" "$scratch/second/archive.nar"; then
    exit 1
fi
