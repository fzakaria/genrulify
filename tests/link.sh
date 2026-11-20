# `link` gives every store path of a shared workspace a package below
# store/, from one of three places: a package of the workspace whose
# archive is the one a binary cache has, the cache, or this machine's
# store. The test emits the example, adds by hand a second package that
# read the example's output, and gives `link` narinfo files in place of a
# cache, so that nothing is fetched.
set -eu
compiler=$1
data=$2
scratch=$(mktemp -d)
trap 'chmod -R u+w "$scratch"; rm -rf "$scratch"' EXIT
workspace=$scratch/mono
"$compiler" import "$data/example" > "$scratch/graph.mlir"
"$compiler" emit --into "$workspace" "$data/example" "$scratch/graph.mlir" > /dev/null
built=l1dx8g9mr73bdp8y6i2yp92xprmz8xzg-genrulify-example
digest=$(sed -n 's/.*"digest": "\(.*\)".*/\1/p' "$workspace/pkgs/$built/package.json")

# A store path this machine has and no cache does: the program that runs
# this test, as a real path of the store. And one that nobody has.
local_path=$(readlink -f "$(command -v sh)")
local_path=$(printf '%s' "$local_path" | cut -d/ -f1-4)
local_name=${local_path#/nix/store/}
gone=/nix/store/zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz-gone

# The second package read the example's output and the local path.
consumer=cccccccccccccccccccccccccccccccc-consumer
mkdir -p "$workspace/pkgs/$consumer"
cat > "$workspace/pkgs/$consumer/package.json" <<JSON
{
 "name": "consumer",
 "package": "pkgs/$consumer",
 "outputs": {"out": {"path": "/nix/store/$consumer", "digest": "$digest", "target": "archive"}},
 "closure": ["/nix/store/$built", "$local_path"]
}
JSON

# What a cache says of a store path. Nix writes a digest in its own
# base 32, in which the digest of the empty string is the one below.
EMPTY_BASE32=0mdqa9w1p6cmli6976v4wi0sw9r4p5prkj7lzfd1877wk11c9c73
EMPTY_HEX=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
narinfo() {
    cat > "$workspace/narinfo/${1%%-*}.narinfo" <<INFO
StorePath: /nix/store/$1
URL: nar/$EMPTY_BASE32.nar.xz
Compression: xz
FileHash: sha256:$EMPTY_BASE32
NarHash: sha256:$2
INFO
}
mkdir -p "$workspace/narinfo"

# The cache has every path of the example's closure, and has the
# example's output under the digest the workspace builds. A path with a
# marker in place of a narinfo is one the cache was asked for and lacks.
while IFS=$'\x1f' read -r path holder; do
    narinfo "${path#/nix/store/}" "$EMPTY_BASE32"
done < "$workspace/pkgs/$built/closure.txt"
narinfo "$built" "$("$compiler" base32 "$digest")"
touch "$workspace/narinfo/${local_name%%-*}.absent" "$workspace/narinfo/${consumer%%-*}.absent"
printf 'example %s\nbad"name %s\n' "$built" "$built" > "$scratch/attrs"

# No cache is reachable: everything is already known.
link() {
    "$compiler" link --cache http://127.0.0.1:1 --attrs "$scratch/attrs" "$workspace"
}
link > "$scratch/report"
cd "$workspace"

# The example's output is the archive its package builds, because the
# cache has the same archive.
grep -q "nix_built(name = \"path\", archive = \"//pkgs/$built:archive\")" "store/$built/BUILD.bazel"
test "$(cat "store/$built/nar.sha256")" = "$digest"

# A path of the closure that no package builds is fetched from the cache
# and held to both digests the cache gave: the file's and the archive's.
first=$(head -n 1 "pkgs/$built/closure.txt")
first=${first%%$'\x1f'*}
fetched=${first#/nix/store/}
grep -q "nix_fetched(name = \"path\", src = \"@nar_${fetched%%-*}//file\", compression = \"xz\", sha256 = \"$EMPTY_HEX\")" "store/$fetched/BUILD.bazel"
test "$(cat "store/$fetched/nar.sha256")" = "$EMPTY_HEX"
grep -A3 "name = \"nar_${fetched%%-*}\"" MODULE.bazel | grep -q "http://127.0.0.1:1/nar/$EMPTY_BASE32.nar.xz"
grep -A3 "name = \"nar_${fetched%%-*}\"" MODULE.bazel | grep -q "sha256 = \"$EMPTY_HEX\""

# A path only this machine has is kept in the workspace as its archive.
grep -q 'nix_source(name = "path")' "store/$local_name/BUILD.bazel"
test "$(sha256sum < "store/$local_name/nar" | cut -d' ' -f1)" = "$(cat "store/$local_name/nar.sha256")"

# Every output has a package too, so that anyone can ask for it.
test -e "store/$consumer/BUILD.bazel"

# An attribute is an alias of its package's outputs. A name no target
# can have is left out.
grep -q "alias(name = \"example\", actual = \"//pkgs/$built:outputs\")" BUILD.bazel
test "$(grep -c '^alias' BUILD.bazel)" = 1

# Bazel's sandbox has to let a rule write the unpacked store paths.
grep -q -e '--sandbox_writable_path=/var/tmp' .bazelrc

# The report counts where the store paths come from.
grep -q '2 packages' "$scratch/report"
grep -q '2 built by a package of the workspace' "$scratch/report"
grep -q '1 kept as an archive in the workspace' "$scratch/report"

# When the cache has another archive for the example's output, the
# consumer read that one, and the workspace fetches it.
narinfo "$built" "$EMPTY_BASE32"
link > "$scratch/report"
grep -q "nix_fetched(name = \"path\", src = \"@nar_${built%%-*}//file\"" "store/$built/BUILD.bazel"
grep -q '1 built by a package of the workspace' "$scratch/report"
grep -q "1 output differs from the cache's" "$scratch/report"

# A store path that is neither built, nor in the cache, nor here stops
# the link, which names the path.
sed -i "s|\"closure\": \[|\"closure\": [\"$gone\", |" "pkgs/$consumer/package.json"
touch "narinfo/zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz.absent"
if link > /dev/null 2> "$scratch/error"; then
    exit 1
fi
grep -q "$gone" "$scratch/error"
