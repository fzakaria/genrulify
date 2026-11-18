# Reject a source outside the store before creating the destination.
set -eu
compiler=$1
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
cat > "$scratch/bad.mlir" <<'MLIR'
module {
  %a = splice.source "/etc/passwd"
  splice.publish %a
}
MLIR
if "$compiler" emit "$scratch/bad.mlir" "$scratch/workspace" 2>"$scratch/err"; then
    exit 1
fi
grep -q 'store file' "$scratch/err"
test ! -e "$scratch/workspace"
