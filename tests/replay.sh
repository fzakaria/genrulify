# A replay that cannot start one of its processes must fail, not hang. The
# plan has two processes joined by a pipe, and the first one's standard
# input is a file that was never staged. Opening it fails before anything
# is started, which used to leave the pipe's supervisor waiting forever for
# readers that would never run. The test gives the replay a few seconds.
set -eu
compiler=$1
data=$2
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

# Emitting the example yields a platform layer to hand the replay.
"$compiler" import "$data/example" > "$scratch/graph.mlir"
"$compiler" emit "$data/example" "$scratch/graph.mlir" "$scratch/workspace"

cat > "$scratch/plan.json" <<'JSON'
{
 "name": "unstartable",
 "closure": [],
 "platform": "platform",
 "environments": [[]],
 "inputs": [],
 "results": [],
 "output": "",
 "timeoutSeconds": 300,
 "processes": [
  {"pid": 1, "step": 1, "exit": 4, "status": 0, "name": "writer", "exe": "/bin/writer",
   "cwd": "/build", "argv": ["writer"], "environment": 0,
   "fds": [{"fd": 0, "target": "/build/never/staged", "mode": 256, "flags": 0, "position": 0},
           {"fd": 1, "target": "pipe:[7]", "mode": 128, "flags": 1, "position": 0}]},
  {"pid": 2, "step": 2, "exit": 4, "status": 0, "name": "reader", "exe": "/bin/reader",
   "cwd": "/build", "argv": ["reader"], "environment": 0,
   "fds": [{"fd": 0, "target": "pipe:[7]", "mode": 256, "flags": 0, "position": 0}]}
 ]
}
JSON
status=0
timeout 20 "$compiler" replay "$scratch/plan.json" "$scratch/result.layer" \
    "$scratch/workspace/platform.layer" 2> "$scratch/error" || status=$?

# 124 is timeout's status for a command it had to kill.
test "$status" != 0
test "$status" != 124
grep -q 'cannot open /build/never/staged' "$scratch/error"
test ! -e "$scratch/result.layer"

# A redirection from a file in the store closure opens the file where the
# closure is, which is the host's store, not the staged tree. The plan
# reads its standard input from a closure path. The process itself cannot
# run in the test, so the replay still fails, but the failure must be the
# process's exit status and not the redirection.
cp "$data/trace/events.jsonl" "$scratch/closure-file"
sed -e "s|/build/never/staged|$scratch/closure-file|" \
    -e "s|\"closure\": \[\]|\"closure\": [\"$scratch\"]|" \
    "$scratch/plan.json" > "$scratch/closure-plan.json"
status=0
timeout 60 "$compiler" replay "$scratch/closure-plan.json" "$scratch/result.layer" \
    "$scratch/workspace/platform.layer" 2> "$scratch/error" || status=$?
test "$status" != 0
test "$status" != 124
if grep -q 'cannot open' "$scratch/error"; then
    cat "$scratch/error" >&2
    exit 1
fi
grep -q 'exited with' "$scratch/error"

# A process that names its output at random, as mktemp does, still writes
# the recorded entry: the same kind, mode and bytes under another name. A
# replay accepts such an entry for a result that is missing and passes it
# on under the recorded name. The plan here runs nothing and expects
# /etc/renamed with the bytes of the platform's /etc/hosts, which the plan
# does not otherwise know about. Packing /etc afterwards shows the result
# under its recorded name.
hosts=$(grep -B4 '"/etc/hosts"' "$data/example/states/platform.json" | grep content | cut -d'"' -f4)
cat > "$scratch/rename-plan.json" <<JSON
{
 "name": "renamer", "closure": [], "platform": "platform", "environments": [],
 "inputs": [], "processes": [], "output": "", "timeoutSeconds": 300,
 "results": [{"path": "/etc/renamed", "kind": "file", "mode": 420, "mtime": 1, "content": "$hosts"}]
}
JSON
"$compiler" replay "$scratch/rename-plan.json" "$scratch/renamer.layer" \
    "$scratch/workspace/platform.layer"
cat > "$scratch/pack-plan.json" <<'JSON'
{
 "name": "etc", "closure": [], "platform": "platform", "environments": [],
 "inputs": [{"layer": "renamer", "path": "/etc/renamed"}], "processes": [],
 "results": [], "output": "/etc/renamed", "timeoutSeconds": 300
}
JSON
"$compiler" pack "$scratch/pack-plan.json" "$scratch/renamed.nar" "$scratch/renamer.layer"
grep -q localhost "$scratch/renamed.nar"

# An entry with other bytes is not a stand-in, and the result stays missing.
sed 's/"content": "[0-9a-f]*"/"content": "0000"/' "$scratch/rename-plan.json" > "$scratch/other-plan.json"
if "$compiler" replay "$scratch/other-plan.json" "$scratch/other.layer" \
    "$scratch/workspace/platform.layer" 2> "$scratch/error"; then
    exit 1
fi
grep -q 'missing /etc/renamed' "$scratch/error"
