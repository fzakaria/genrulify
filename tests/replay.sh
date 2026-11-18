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
