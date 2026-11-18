# A NAR contains executable bits and symlinks. Mixing byte-file and NAR
# values must fail MLIR verification before a workspace can be emitted.
set -eu
compiler=$1
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
cat > "$scratch/ok.mlir" <<'MLIR'
module {
  %0 = splice.seed "source.nar"
  %1 = splice.exec %0 "compile" {step = 12 : i64}
  splice.package %1
}
MLIR
"$compiler" opt "$scratch/ok.mlir" > "$scratch/folded"
grep -q splice.exec "$scratch/folded"
sed 's/splice.seed/splice.source/' "$scratch/ok.mlir" > "$scratch/bad.mlir"
if "$compiler" opt "$scratch/bad.mlir" 2> "$scratch/diagnostic"; then
  exit 1
fi
grep -q 'type' "$scratch/diagnostic"
