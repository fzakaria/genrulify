# Lowering writes one rule per operation for both build systems. The
# test emits the example recording and checks that each rule names the real
# files it reads and writes and shows the recorded command, that the
# workspace holds everything the commands refer to, and that emit will not
# write into an existing directory.
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
    test "$(grep -c '^[a-z_]*($' "$file")" = 4
    grep -q 'name = "s764_dd"' "$file"
    grep -q 'name = "archive"' "$file"

    # A rule's command is the recorded command line, run by the script that
    # ships in the workspace. Nothing in a build file calls the compiler.
    grep -q 'bin/dd if=.*-body.txt of=bundle oflag=append conv=notrunc status=none' "$file"
    if grep -q "bin/genrulify" "$file"; then
        exit 1
    fi
done

# The append reads the version of /build/bundle the first dd wrote and
# writes its own, as real files in both systems.
rule() {
    sed -n "/name = \"$2\"/,/^)/p" "$1"
}
rule BUILD.bazel s764_dd | grep -q '":s735_dd/build/bundle"'
rule BUILD.bazel s764_dd | grep -q '"s764_dd/build/bundle"'
rule BUILD.bazel s764_dd | grep -q '"./sandbox.sh --in . --in $(RULEDIR) --out $(RULEDIR)"'

# Buck2 hands a command all of its sources in one environment variable,
# which the kernel caps, so a Buck2 rule writes one directory and reads the
# directories of the rules and of the recorded bytes it uses. The manifest
# still names each file.
rule BUCK s764_dd | grep -q '":s735_dd",'
rule BUCK s764_dd | grep -q '"platform",'
rule BUCK s764_dd | grep -q 'out = "s764_dd",'
rule BUCK s764_dd | grep -q '"\$SRCDIR/sandbox.sh --in \$SRCDIR --out \${OUT%/\*}"'
rule BUCK archive | grep -q 'out = "archive.nar",'

# The archive rule takes the last dd's file and writes a NAR.
rule BUILD.bazel archive | grep -q '"archive.nar"'
rule BUILD.bazel archive | grep -q 'pack /nix/store/l1dx8g9mr73bdp8y6i2yp92xprmz8xzg-genrulify-example'

# The workspace carries what the commands refer to by name: the script, the
# closure, the environment, the guest's /etc and each rule's manifest, which
# holds the entry the rule must reproduce.
test -x sandbox.sh
test -s closure.txt
test -s env/0.env
test -s platform/etc/hosts
grep -q '50f55425c6b2fa76153714d7097567804eabbe75984a18b4534537973c24d9fa' actions/s764_dd.manifest

# A copy is read under the name of the file it copies. With every pass
# run, the last dd is a copy of the bundle, so two commands are left, and
# the archive rule lists the appended bundle and stages it at the output
# path.
"$compiler" opt "$scratch/graph.mlir" > "$scratch/optimized.mlir"
"$compiler" emit "$data/example" "$scratch/optimized.mlir" "$scratch/optimized"
test "$(grep -c '^[a-z_]*($' "$scratch/optimized/BUILD.bazel")" = 3
rule "$scratch/optimized/BUILD.bazel" archive | grep -q '":s764_dd/build/bundle"'
grep -q "/nix/store/l1dx8g9mr73bdp8y6i2yp92xprmz8xzg-genrulify-example.*s764_dd/build/bundle" \
    "$scratch/optimized/actions/archive.manifest"

# A group that `check` did not replay must not become a rule. A report made
# with every pass says the last dd is a copy, so lowering the graph with
# that dd still in it is refused.
cp -r "$data/example" "$scratch/reported"
chmod -R u+w "$scratch/reported"
cat > "$scratch/reported/check.json" <<'JSON'
{"groups": [{"step": 782, "name": "dd", "processes": 1, "outcome": "copy", "inputs": 0, "pinned": []}]}
JSON
if "$compiler" emit "$scratch/reported" "$scratch/graph.mlir" "$scratch/unchecked" 2> "$scratch/error"; then
    exit 1
fi
grep -q 'was not replayed' "$scratch/error"
"$compiler" emit "$scratch/reported" "$scratch/optimized.mlir" "$scratch/checked-workspace"

# A file of the build can be named like a build file: zlib's tarball has a
# BUILD.bazel in it. Checked into the workspace under that name, the file
# would make its directory a package of its own and every label into the
# directory invalid, so its artifact is given another name.
sed 's|^  splice.output|  %9 = splice.literal "glue" at 749 -> [\n      #splice.entry<"/build/sub/BUILD.bazel" file 644 mtime 1 "cb94740ad765d0c3cb3742959e2f7556d4c7adb712660bfbacb1ba2c6f50e671">\n  ]\n&|' \
    "$scratch/graph.mlir" > "$scratch/named.mlir"
"$compiler" emit "$data/example" "$scratch/named.mlir" "$scratch/named"
test -e "$scratch/named/glue_749/build/sub/=42UILD.bazel"
test -z "$(find "$scratch/named" -mindepth 2 -name BUILD.bazel)"

# A process substitution leaves a pipe on a high descriptor, and the bytes
# the builder had written into it are part of the recording. The rule
# wires that descriptor to a file of those bytes, named after the pipe so
# that every descriptor on the pipe shares the one file. The fixture has no
# such pipe, so each process's standard input is turned into one.
cp -r "$data/example" "$scratch/piped"
chmod -R u+w "$scratch/piped"
sed -i '/"fd": 0,/,/"target"/ {
    s|"fd": 0,|"fd": 63,|
    s|"input": null|"input": "6869"|
    s|"target": "/dev/null"|"target": "pipe:[77]"|
}' "$scratch/piped/recording.json"
"$compiler" import "$scratch/piped" > "$scratch/piped.mlir"
"$compiler" emit "$scratch/piped" "$scratch/piped.mlir" "$scratch/piped-workspace"
grep -q -- '--fd 63=data:actions/s764_dd.pipe77.data' "$scratch/piped-workspace/BUILD.bazel"
grep -q '":actions/s764_dd.pipe77.data"' "$scratch/piped-workspace/BUILD.bazel"
test "$(cat "$scratch/piped-workspace/actions/s764_dd.pipe77.data")" = hi

# An existing destination is left alone.
if "$compiler" emit "$data/example" "$scratch/graph.mlir" "$scratch/workspace" 2> /dev/null; then
    exit 1
fi

# The description lists every operation and every process group as JSON.
# It is made from the module before optimization and marks the operations
# that elimination removes, so a page can draw the graph both ways. The
# example has nothing to eliminate, so an operation nothing reads is added.
cd "$scratch"
sed 's|^  splice.output|  %9 = splice.exec "ls" at 749(%dd_735) -> [\n      #splice.entry<"/build/listing" file 644 mtime 1 "00">\n  ]\n&|' \
    "$scratch/graph.mlir" > "$scratch/recorded.mlir"
"$compiler" describe "$data/example" "$scratch/recorded.mlir" > "$scratch/description.json"
grep -q '"id":"s764_dd"' "$scratch/description.json"
grep -q '"node":"s735_dd"' "$scratch/description.json"
grep -q '"op":"output"' "$scratch/description.json"
grep -q 'oflag=append' "$scratch/description.json"

# The description has the graph under every choice of passes, so that a
# page can switch any of them on and off. It names the six passes, lists
# each distinct node once, and gives one list of nodes per choice: two to
# the power of six.
grep -q '"passes":\["link","dce","fold","merge","raise","rules"\]' "$scratch/description.json"
test "$(grep -o '"variants":\[.*\]\]' "$scratch/description.json" | grep -o '\],\[' | wc -l)" = 63

# The added ls is one node however many choices keep it, and the last dd
# ends as a copy. A node says which values it defines, by name.
test "$(grep -o '"id":"s749_ls"' "$scratch/description.json" | wc -l)" = 1
grep -q '"id":"copy_782_dd"' "$scratch/description.json"
grep -q '"defines":\["dd_764"\]' "$scratch/description.json"

# A description carries what the checker reported for each group, so a page
# can say how many entries a replayed group was given.
cp -r "$data/example" "$scratch/checked"
chmod -R u+w "$scratch/checked"
cat > "$scratch/checked/check.json" <<'JSON'
{"groups": [{"step": 764, "name": "dd", "processes": 1, "outcome": "traced", "inputs": 2, "pinned": []}]}
JSON
"$compiler" describe "$scratch/checked" "$scratch/graph.mlir" > "$scratch/checked.json"
grep -q '"inputs":2' "$scratch/checked.json"
grep -q '"outcome":"traced"' "$scratch/checked.json"
