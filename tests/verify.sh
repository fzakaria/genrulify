# A corrupt backend archive must disagree with both reference hashes.
set -eu
compiler=$1
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
printf abc > "$scratch/reference"
printf bad > "$scratch/backend"
digest=$(sha256sum "$scratch/reference" | cut -d' ' -f1)
printf '{"ending":"exited:0","outputs":[{"hash":"sha256:%s"}]}' "$digest" > "$scratch/run"
"$compiler" verify "$scratch/run" "$scratch/reference" "$scratch/reference"
if "$compiler" verify "$scratch/run" "$scratch/reference" "$scratch/backend"; then
    exit 1
fi
