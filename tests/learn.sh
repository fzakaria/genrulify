# A build is one more sample of every rule. When a rule fails to reproduce
# the recording during a real build, `learn` reads the failure out of the
# build log and adjusts the verdicts: a result that differed is pinned, and
# a process that ended differently demotes its group. The test feeds two
# fabricated logs to a copy of the example recording and checks both the
# verdicts and what the next import does with them.
set -eu
compiler=$1
data=$2
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
cp -r "$data/example" "$scratch/recording"
chmod -R u+w "$scratch/recording"

# A log with nothing in it teaches nothing, and says so with its status.
if printf 'INFO: Build completed\n' | "$compiler" learn "$scratch/recording"; then
    exit 1
fi
test ! -e "$scratch/recording/verdicts.json"

# A result that differed is pinned to the recording. The line is indented
# and surrounded by other output, as it is in a Bazel log.
cat > "$scratch/differs.log" <<'LOG'
ERROR: BUILD.bazel:9:8: Executing genrule //:s764_dd failed: (Exit 1)
nixsplice: s764_dd did not reproduce the recording:
  differs /build/bundle: got file 644 aa, recorded file 644 bb
Target //:archive failed to build
LOG
"$compiler" learn "$scratch/recording" < "$scratch/differs.log"
grep -q '"/build/bundle"' "$scratch/recording/verdicts.json"
grep -q '"narrow"' "$scratch/recording/verdicts.json"
"$compiler" import "$scratch/recording" > "$scratch/pinned.mlir"
grep -q 'splice.literal "dd" at 764 demoted' "$scratch/pinned.mlir"
grep -q 'splice.exec "dd" at 764' "$scratch/pinned.mlir"

# Learning the same failure again changes nothing.
if "$compiler" learn "$scratch/recording" < "$scratch/differs.log"; then
    exit 1
fi

# A process that ended differently cannot be trusted with any result.
cat > "$scratch/status.log" <<'LOG'
nixsplice: s782_dd did not reproduce the recording:
  dd exited with 1, recorded 0
  missing /nix/store/r8mgc2hqpzggmvmscm94dynii1wk1ygm-nixsplice-example
LOG
"$compiler" learn "$scratch/recording" < "$scratch/status.log"
"$compiler" import "$scratch/recording" > "$scratch/demoted.mlir"
grep -q 'splice.literal "dd" at 782 demoted' "$scratch/demoted.mlir"
if grep -q 'splice.exec "dd" at 782' "$scratch/demoted.mlir"; then
    exit 1
fi
