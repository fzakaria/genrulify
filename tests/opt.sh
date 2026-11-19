# The optimizer's passes, one at a time. Each case hands `opt` a small
# module and the passes to run, and looks at what is left.
#
# Linking ties a file the driver wrote to the file its bytes came from.
# Dead-code elimination is what the Pure trait buys. Folding moves results
# that need no bytes computed out of their process group. Merging and
# raising turn a result with the bytes of an operand into a copy of the
# operand, at the same path and at another one.
set -eu
compiler=$1
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

# Fails if the optimized module still has an operation labelled $1.
gone() {
    if grep -q "\"$1\"" "$scratch/opt.mlir"; then
        echo "$1 survived" >&2
        exit 1
    fi
}

# Elimination: the module has a compile that feeds the output, a process
# that wrote nothing, a temporary file nothing reads, and builder
# bookkeeping. Only the compile and the sandbox it runs in survive.
cat > "$scratch/graph.mlir" <<'MLIR'
module {
  %0 = splice.literal "sandbox" at 1 -> [#splice.entry<"/build" dir 700 mtime 1>]
  %1 = splice.literal "glue" at 2 -> [#splice.entry<"/build/env-vars" file 600 mtime 1 "aa">]
  splice.exec "date" at 3(%0) -> []
  %2 = splice.exec "mktemp" at 4(%0) -> [#splice.entry<"/build/tmp.x" file 600 mtime 1 "bb">]
  %3 = splice.exec "cc" at 5(%0) -> [#splice.entry<"/out" file 755 mtime 1 "cc">]
  splice.output "out" "/out"(%3)
}
MLIR
"$compiler" opt "$scratch/graph.mlir" dce > "$scratch/opt.mlir"
grep -q 'splice.exec "cc"' "$scratch/opt.mlir"
grep -q 'splice.literal "sandbox"' "$scratch/opt.mlir"
for dead in date mktemp glue; do
    gone "$dead"
done

# The other passes share one module: a directory and an empty file made by
# two commands, a chmod that leaves a file's bytes alone, a compile, and
# two commands that copy the compiler's output.
cat > "$scratch/build.mlir" <<'MLIR'
module {
  %0:2 = splice.literal "sandbox" at 1 -> [
      #splice.entry<"/build" dir 700 mtime 1>,
      #splice.entry<"/build/main.c" file 644 mtime 1 "aa">]
  %1 = splice.exec "mkdir" at 2(%0#0) -> [#splice.entry<"/out" dir 755 mtime 2>]
  %2 = splice.exec "touch" at 3(%0#0) -> [#splice.entry<"/build/stamp" file 644 mtime 3 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855">]
  %3 = splice.exec "chmod" at 4(%0#0, %0#1) -> [#splice.entry<"/build/main.c" file 600 mtime 1 "aa">]
  %4 = splice.exec "cc" at 5(%0#0, %3, %2) -> [#splice.entry<"/build/a.out" file 755 mtime 5 "bb">]
  %5 = splice.exec "install" at 6(%0#0, %1, %4) -> [#splice.entry<"/out/prog" file 755 mtime 6 "bb">]
  %6 = splice.exec "cp" at 7(%0#0, %1, %5) -> [#splice.entry<"/out/copy" file 755 mtime 7 "bb">]
  splice.output "out" "/out"(%1, %5, %6)
}
MLIR

# Elimination alone removes nothing here: every command feeds the output.
"$compiler" opt "$scratch/build.mlir" dce > "$scratch/opt.mlir"
test "$(grep -c 'splice.exec' "$scratch/opt.mlir")" = 6

# Folding: a directory and an empty file are constants, so mkdir and touch
# are no longer run. The commands with bytes to compute are untouched.
"$compiler" opt "$scratch/build.mlir" dce fold > "$scratch/opt.mlir"
grep -q 'splice.literal "mkdir" at 2 folded' "$scratch/opt.mlir"
grep -q 'splice.literal "touch" at 3 folded' "$scratch/opt.mlir"
test "$(grep -c 'splice.exec' "$scratch/opt.mlir")" = 4

# A pass removes nothing by itself. Without elimination the constants are
# there and so are the commands that no longer have a reader.
"$compiler" opt "$scratch/build.mlir" fold > "$scratch/opt.mlir"
grep -q 'splice.literal "mkdir" at 2 folded' "$scratch/opt.mlir"
grep -q 'splice.exec "mkdir" at 2' "$scratch/opt.mlir"

# Merging: chmod wrote the bytes it was given at the same path, so its
# result is a copy of its operand. install wrote them elsewhere and stays.
"$compiler" opt "$scratch/build.mlir" dce fold merge > "$scratch/opt.mlir"
grep -q 'splice.copy "chmod" at 4' "$scratch/opt.mlir"
grep -q 'splice.exec "install"' "$scratch/opt.mlir"

# Raising: install and cp become copies, and a copy of a copy is a copy of
# the original, so both name the compiler's result. With no passes named,
# all of them run, and the compile is the one command left.
"$compiler" opt "$scratch/build.mlir" > "$scratch/opt.mlir"
test "$(grep -c 'splice.exec' "$scratch/opt.mlir")" = 1
grep -q 'splice.copy "install" at 6(%cc_5)' "$scratch/opt.mlir"
grep -q 'splice.copy "cp" at 7(%cc_5)' "$scratch/opt.mlir"

# Linking covers a file the driver wrote itself, which is a literal. cargo
# links a compiled binary to its final name in-process, so with cargo
# opened the final name is driver-written. A literal file with the bytes of
# a file that already exists is a copy of the latest such file, and so the
# output is the compiler's bytes and not recorded ones. Without the link,
# elimination deletes the compiler.
cat > "$scratch/linked.mlir" <<'MLIR'
module {
  %0 = splice.literal "sandbox" at 1 -> [#splice.entry<"/build" dir 700 mtime 1>]
  %1 = splice.exec "rustc" at 2(%0) -> [#splice.entry<"/build/deps/prog-1f" file 755 mtime 2 "dd">]
  %2:2 = splice.literal "glue" at 3 -> [
      #splice.entry<"/out/prog" file 755 mtime 3 "dd">,
      #splice.entry<"/out/note" file 644 mtime 3 "ee">]
  splice.output "out" "/out"(%2#0, %2#1)
}
MLIR
"$compiler" opt "$scratch/linked.mlir" > "$scratch/opt.mlir"
grep -q 'splice.copy "glue" at 3(%rustc_2) -> <"/out/prog"' "$scratch/opt.mlir"
grep -q 'splice.literal "glue" at 3' "$scratch/opt.mlir"
"$compiler" opt "$scratch/linked.mlir" dce > "$scratch/opt.mlir"
gone rustc

# A second link of the same bytes is also a copy of the compiler's file,
# and not of the first link, which is itself only a name for that file.
sed 's|^  splice.output|  %4 = splice.literal "glue" at 4 -> [#splice.entry<"/out/again" file 755 mtime 4 "dd">]\n&|; s|(%2#0, %2#1)|(%2#0, %2#1, %4)|' \
    "$scratch/linked.mlir" > "$scratch/twice.mlir"
"$compiler" opt "$scratch/twice.mlir" link > "$scratch/opt.mlir"
grep -q 'splice.copy "glue" at 4(%rustc_2) -> <"/out/again"' "$scratch/opt.mlir"

# An unknown pass is an error.
if "$compiler" opt "$scratch/build.mlir" inline 2> /dev/null; then
    exit 1
fi

# A project's graph holds its source tree, and its rules have to stay
# right when a source is edited. A command that read a source and left the
# bytes of its input, or an empty file, did so for the bytes the source
# had when the build was recorded. `sort` of a sorted file is the example.
# Such a command stays a command, and keeps every result it has. That
# includes a directory: a command can make one only when a source says
# so, and a build that stopped making it must fail, not package a
# directory from the recording. A command that read no source is
# rewritten as before. So is a command that was given only a directory a
# source-reading command made: a directory has no bytes for the edit to
# change. Here that is a mktemp.
cat > "$scratch/project.mlir" <<'MLIR'
module {
  %0 = splice.literal "sandbox" at 1 -> [#splice.entry<"/build" dir 700 mtime 1>]
  %1:2 = splice.literal "source" at 1 -> [
      #splice.entry<"/nix/store/source" dir 555 mtime 1>,
      #splice.entry<"/nix/store/source/names" file 444 mtime 1 "aa">]
  %2:3 = splice.exec "sort" at 2(%0, %1#0, %1#1) -> [
      #splice.entry<"/out" dir 755 mtime 2>,
      #splice.entry<"/out/sorted" file 644 mtime 2 "aa">,
      #splice.entry<"/out/errors" file 644 mtime 2 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855">]
  %3 = splice.exec "cc" at 3(%0) -> [#splice.entry<"/build/prog" file 755 mtime 3 "cc">]
  %4 = splice.exec "install" at 4(%0, %3) -> [#splice.entry<"/out/prog" file 755 mtime 4 "cc">]
  %5 = splice.exec "mktemp" at 5(%2#0) -> [#splice.entry<"/out/tmp.x" file 600 mtime 5 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855">]
  splice.output "out" "/out"(%2#0, %2#1, %2#2, %4, %5)
}
MLIR
"$compiler" opt "$scratch/project.mlir" > "$scratch/opt.mlir"
if grep -q 'splice.exec "mktemp"' "$scratch/opt.mlir"; then
    exit 1
fi
grep -q 'splice.exec "sort" at 2' "$scratch/opt.mlir"
grep -q 'splice.copy "install" at 4(%cc_3)' "$scratch/opt.mlir"
if grep -q 'splice.exec "install"' "$scratch/opt.mlir"; then
    exit 1
fi
if grep -q 'splice.copy "sort"' "$scratch/opt.mlir"; then
    exit 1
fi
if grep -q 'splice.literal "sort" at 2 folded' "$scratch/opt.mlir"; then
    exit 1
fi
grep -q 'splice.output "out" "/out"(%sort_2#0, %sort_2#1, %sort_2#2' "$scratch/opt.mlir"

# A command that read a source and left nothing a later step reads is
# dead in a recorded build. In a project it is not: after an edit the
# same command may write a file it did not write before, or end another
# way, and the build the rules replay would no longer be the build Nix
# runs. Such a command is marked `watched` and kept, so that its rule
# runs and can fail. Here a generator read a flag and wrote only a log
# nothing reads. A command that read no source and is dead goes as
# before.
cat > "$scratch/watched.mlir" <<'MLIR'
module {
  %0 = splice.literal "sandbox" at 1 -> [#splice.entry<"/build" dir 700 mtime 1>]
  %1:2 = splice.literal "source" at 1 -> [
      #splice.entry<"/nix/store/source" dir 555 mtime 1>,
      #splice.entry<"/nix/store/source/flag" file 444 mtime 1 "aa">]
  %2 = splice.exec "generate" at 2(%0, %1#0, %1#1) -> [#splice.entry<"/build/log" file 644 mtime 2 "bb">]
  %3 = splice.exec "probe" at 3(%0) -> [#splice.entry<"/build/probe.log" file 644 mtime 3 "cc">]
  %4 = splice.exec "cc" at 4(%0) -> [#splice.entry<"/out" file 755 mtime 4 "dd">]
  splice.output "out" "/out"(%4)
}
MLIR
"$compiler" opt "$scratch/watched.mlir" > "$scratch/opt.mlir"
grep -q 'splice.exec "generate" at 2 watched(%sandbox_1, %source_1#0, %source_1#1)' "$scratch/opt.mlir"
if grep -q 'splice.exec "probe"' "$scratch/opt.mlir"; then
    exit 1
fi
if grep -q 'splice.exec "cc" at 4 watched' "$scratch/opt.mlir"; then
    exit 1
fi

# The mark is printed and parsed like any other part of the operation.
"$compiler" opt "$scratch/opt.mlir" | grep -q 'splice.exec "generate" at 2 watched('

# A graph without a source tree has nothing to watch.
grep -v '"source"\|/nix/store/source' "$scratch/watched.mlir" | sed 's|(%0, %1#0, %1#1)|(%0)|' > "$scratch/plain.mlir"
"$compiler" opt "$scratch/plain.mlir" > "$scratch/opt.mlir"
if grep -q 'watched\|splice.exec "generate"' "$scratch/opt.mlir"; then
    exit 1
fi

# A file the builder wrote itself has no command to keep. In a recorded
# build, a builder-written file with the bytes of an existing file is
# linked to that file. In a project that would be a guess the next edit
# can prove wrong: a builder that writes the same word a source happens
# to hold never read the source. The file stays recorded bytes.
cat > "$scratch/constant.mlir" <<'MLIR'
module {
  %0 = splice.literal "sandbox" at 1 -> [#splice.entry<"/build" dir 700 mtime 1>]
  %1:2 = splice.literal "source" at 1 -> [
      #splice.entry<"/nix/store/source" dir 555 mtime 1>,
      #splice.entry<"/nix/store/source/message.txt" file 444 mtime 1 "aa">]
  %2 = splice.exec "cp" at 2(%0, %1#0, %1#1) -> [#splice.entry<"/build/message.txt" file 644 mtime 2 "aa">]
  %3 = splice.literal "glue" at 3 -> [#splice.entry<"/out" file 644 mtime 3 "aa">]
  splice.output "out" "/out"(%3)
}
MLIR
"$compiler" opt "$scratch/constant.mlir" > "$scratch/opt.mlir"
grep -q 'splice.literal "glue" at 3' "$scratch/opt.mlir"
if grep -q 'splice.copy' "$scratch/opt.mlir"; then
    exit 1
fi
grep -q 'splice.output "out" "/out"(%glue_3)' "$scratch/opt.mlir"
