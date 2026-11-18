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
compiled=$(sed -n 's/^ *\(%[0-9]*\) = splice.exec "cc".*/\1/p' "$scratch/opt.mlir")
grep -q "splice.copy \"install\" at 6($compiled)" "$scratch/opt.mlir"
grep -q "splice.copy \"cp\" at 7($compiled)" "$scratch/opt.mlir"

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
compiled=$(sed -n 's/^ *\(%[0-9]*\) = splice.exec "rustc".*/\1/p' "$scratch/opt.mlir")
grep -q "splice.copy \"glue\" at 3($compiled) -> <\"/out/prog\"" "$scratch/opt.mlir"
grep -q 'splice.literal "glue" at 3' "$scratch/opt.mlir"
"$compiler" opt "$scratch/linked.mlir" dce > "$scratch/opt.mlir"
gone rustc

# An unknown pass is an error.
if "$compiler" opt "$scratch/build.mlir" inline 2> /dev/null; then
    exit 1
fi
