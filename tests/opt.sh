# Dead-code elimination is the optimization the Pure trait buys. The module
# has a compile that feeds the output, a process that wrote nothing, a
# temporary file nothing reads, and builder bookkeeping. Only the compile
# and the sandbox it runs in survive.
set -eu
compiler=$1
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
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
"$compiler" opt "$scratch/graph.mlir" > "$scratch/opt.mlir"
grep -q 'splice.exec "cc"' "$scratch/opt.mlir"
grep -q 'splice.literal "sandbox"' "$scratch/opt.mlir"
for dead in date mktemp glue; do
    if grep -q "\"$dead\"" "$scratch/opt.mlir"; then
        echo "$dead survived" >&2
        exit 1
    fi
done
