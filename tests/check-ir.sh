# Parsing a broken append must fail even when every operand has file type.
set -eu
compiler=$1
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
cat > "$scratch/bad.mlir" <<'MLIR'
module {
  %a = splice.source "/nix/store/input"
  %b = splice.append %a {path = "/build/a", step = 1 : i64}
  splice.publish %b
}
MLIR
if "$compiler" opt "$scratch/bad.mlir" > /dev/null 2>"$scratch/error"; then
    exit 1
fi
grep -q 'expected' "$scratch/error"
