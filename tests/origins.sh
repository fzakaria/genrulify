# A matching archive says the output is exact. Where each file of the
# output came from is a separate question, and the group counts do not
# answer it: a file the builder's shell wrote is in no group. The test
# hands `origins` a small graph over the example recording, whose blobs
# give the files their sizes, and reads the breakdown of one output.
set -eu
compiler=$1
data=$2
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

# A blob of the example recording, for content with a known size.
bytes=$(ls "$data/example/blobs" | head -n 1)
size=$(wc -c < "$data/example/blobs/$bytes")
empty=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855

# The output holds a directory, a file a command wrote, a copy of that
# file, an empty file, a file the builder wrote itself and a file of a
# command that could not be replayed.
cat > "$scratch/graph.mlir" <<MLIR
module {
  %0 = splice.literal "sandbox" at 1 -> [#splice.entry<"/build" dir 700 mtime 1>]
  %1:2 = splice.literal "glue" at 2 -> [
      #splice.entry<"/out" dir 755 mtime 2>,
      #splice.entry<"/out/note" file 644 mtime 2 "$bytes">]
  %2:2 = splice.exec "cc" at 3(%0) -> [
      #splice.entry<"/out/prog" file 755 mtime 3 "$bytes">,
      #splice.entry<"/out/stamp" file 644 mtime 3 "$empty">]
  %3 = splice.copy "install" at 4(%2#0) -> <"/out/prog2" file 755 mtime 4 "$bytes">
  %4 = splice.literal "strip" at 5 demoted -> [#splice.entry<"/out/stripped" file 755 mtime 5 "$bytes">]
  splice.output "out" "/out"(%1#0, %1#1, %2#0, %2#1, %3, %4)
}
MLIR
"$compiler" origins "$data/example" "$scratch/graph.mlir" > "$scratch/origins"
cat > "$scratch/expected" <<TEXT
out: 2 files ($((2 * size)) bytes) from commands that run, 2 files ($((2 * size)) bytes) from the recording, 2 entries without bytes
  /out/note: written by the builder
  /out/stripped: from a command that is not run
TEXT
diff "$scratch/expected" "$scratch/origins"

# An output made only by commands has nothing to list.
cat > "$scratch/computed.mlir" <<MLIR
module {
  %0 = splice.literal "sandbox" at 1 -> [#splice.entry<"/build" dir 700 mtime 1>]
  %1:2 = splice.exec "cc" at 3(%0) -> [
      #splice.entry<"/out" dir 755 mtime 3>,
      #splice.entry<"/out/prog" file 755 mtime 3 "$bytes">]
  %2 = splice.copy "install" at 4(%1#1) -> <"/out/prog2" file 755 mtime 4 "$bytes">
  splice.output "out" "/out"(%1#0, %1#1, %2)
}
MLIR
"$compiler" origins "$data/example" "$scratch/computed.mlir" > "$scratch/origins"
test "$(cat "$scratch/origins")" = "out: 2 files ($((2 * size)) bytes) from commands that run, 0 files (0 bytes) from the recording, 1 entry without bytes"

# In a project the recorded bytes matter more: they are what an edit will
# not change. The breakdown lists each recorded file that is still in
# use, with why the file is recorded and who uses it. Here the builder
# wrote a file two commands read, and one file of a command that runs is
# pinned to the recording and goes into the output.
cat > "$scratch/project.mlir" <<MLIR
module {
  %0 = splice.literal "sandbox" at 1 -> [#splice.entry<"/build" dir 700 mtime 1>]
  %1:2 = splice.literal "source" at 1 -> [
      #splice.entry<"/nix/store/source" dir 555 mtime 1>,
      #splice.entry<"/nix/store/source/main.c" file 444 mtime 1 "$bytes">]
  %2 = splice.literal "glue" at 2 -> [#splice.entry<"/build/config.h" file 644 mtime 2 "$bytes">]
  %3 = splice.literal "cc" at 3 demoted -> [#splice.entry<"/out/log" file 644 mtime 3 "$bytes">]
  %4:2 = splice.exec "cc" at 3(%0, %1#1, %2) -> [
      #splice.entry<"/out" dir 755 mtime 3>,
      #splice.entry<"/out/prog" file 755 mtime 3 "$bytes">]
  %5 = splice.exec "strip" at 4(%0, %2, %4#1) -> [#splice.entry<"/out/prog" file 755 mtime 4 "$bytes">]
  splice.output "out" "/out"(%4#0, %5, %3)
}
MLIR
"$compiler" origins "$data/example" "$scratch/project.mlir" > "$scratch/origins"
cat > "$scratch/expected" <<TEXT
out: 1 file ($size bytes) from commands that run, 1 file ($size bytes) from the recording, 1 entry without bytes
  /out/log: pinned to the recording while its command runs
recorded bytes that an edit to the project will not change:
  /build/config.h: written by the builder, read by 2 commands
  /out/log: pinned to the recording while its command runs, in output out
TEXT
diff "$scratch/expected" "$scratch/origins"

# A project's graph need not hold its source tree: when no command that
# matters read a source, the tree is dead and gone from the graph. The
# recording still says the build is a project's, and the recorded bytes
# are still listed. Here the builder wrote the whole output.
cp -r "$data/example" "$scratch/recording"
chmod -R u+w "$scratch/recording"
sed -i 's|^ "run":| "source": "/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-source",\n "run":|' "$scratch/recording/recording.json"
cat > "$scratch/constant.mlir" <<MLIR
module {
  %0 = splice.literal "glue" at 2 -> [#splice.entry<"/out" file 644 mtime 2 "$bytes">]
  splice.output "out" "/out"(%0)
}
MLIR
"$compiler" origins "$scratch/recording" "$scratch/constant.mlir" > "$scratch/origins"
grep -q '^recorded bytes that an edit to the project will not change:$' "$scratch/origins"
grep -q '^  /out: written by the builder, in output out$' "$scratch/origins"
"$compiler" origins "$data/example" "$scratch/constant.mlir" > "$scratch/origins"
if grep -q 'an edit to the project' "$scratch/origins"; then
    exit 1
fi
