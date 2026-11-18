# Lowering writes one genrule per operation for both build systems. The
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
    test "$(grep -c '^genrule(' "$file")" = 4
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
sed 's|^  splice.output|  %9 = splice.exec "ls" at 749(%1) -> [\n      #splice.entry<"/build/listing" file 644 mtime 1 "00">\n  ]\n&|' \
    "$scratch/graph.mlir" > "$scratch/recorded.mlir"
"$compiler" describe "$data/example" "$scratch/recorded.mlir" > "$scratch/description.json"
grep -q '"dead":true,"id":"s749_ls"' "$scratch/description.json"
grep -q '"dead":false,"id":"s764_dd"' "$scratch/description.json"
grep -q '"id":"s764_dd"' "$scratch/description.json"
grep -q '"node":"s735_dd"' "$scratch/description.json"
grep -q '"op":"output"' "$scratch/description.json"
grep -q 'oflag=append' "$scratch/description.json"

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
