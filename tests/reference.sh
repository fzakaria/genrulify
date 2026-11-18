# The reference archive is the output as the recording saw it. Packing the
# example exercises writing a layer, reading it back, staging its entries
# and dumping a NAR, and the digest must be the one Nix computed for the
# same output. A second, corrupted archive must fail verification.
set -eu
compiler=$1
data=$2
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
"$compiler" reference "$data/example" out "$scratch/reference.nar"
test "$(sha256sum "$scratch/reference.nar" | cut -d' ' -f1)" = \
    1c763cb553edd9c4e7632da40b6b2466b54b89b818844c85ed2033df7cf9eab9
"$compiler" verify "$scratch/reference.nar" "$scratch/reference.nar" | grep -q '^sha256:1c763cb5'

printf corrupt > "$scratch/other.nar"
if "$compiler" verify "$scratch/reference.nar" "$scratch/other.nar" 2> "$scratch/error"; then
    exit 1
fi
grep -q 'NAR mismatch' "$scratch/error"

# An output the derivation does not have is an error.
if "$compiler" reference "$data/example" dev "$scratch/dev.nar" 2> /dev/null; then
    exit 1
fi
