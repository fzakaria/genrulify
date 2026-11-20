# In a shared workspace a store path reaches a step as an archive some
# rule made, not as a path on the host. The test drives the sandbox
# script directly: it packs a small tree as a NAR, has the script import
# a compressed copy the way a fetched store path arrives, and then runs a
# step whose closure names the archive, which the script has to unpack
# once into its own store, as Nix would have left the path.
set -eu
compiler=$1
data=$2
scratch=$(mktemp -d)
trap 'chmod -R u+w "$scratch"; rm -rf "$scratch"' EXIT

# Emitting the example yields the script.
"$compiler" import "$data/example" > "$scratch/graph.mlir"
"$compiler" emit "$data/example" "$scratch/graph.mlir" "$scratch/workspace"
script=$scratch/workspace/sandbox.sh

# A manifest line: tag, kind, mode, mtime, path, content and artifact,
# separated by the unit separator.
line() {
    printf '%s\x1f%s\x1f%s\x1f%s\x1f%s\x1f%s\x1f%s\n' "$@"
}
bundle=cb94740ad765d0c3cb3742959e2f7556d4c7adb712660bfbacb1ba2c6f50e671
mkdir -p "$scratch/actions"

# Pack a store path with a program, a plain file and a symlink.
name=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-thing
{
    line in dir 755 1 "/nix/store/$name" '' ''
    line in dir 755 1 "/nix/store/$name/bin" '' ''
    line in file 755 1 "/nix/store/$name/bin/tool" "$bundle" "blobs/$bundle"
    line in file 644 1 "/nix/store/$name/notes" "$bundle" "blobs/$bundle"
    line in link 0 1 "/nix/store/$name/latest" notes ''
} > "$scratch/actions/thing.manifest"
"$script" --name thing --in "$scratch" --in "$data/example" --out "$scratch/out" pack "/nix/store/$name"
nar=$scratch/out/thing.nar
digest=$(sha256sum < "$nar" | cut -d' ' -f1)

# A fetched store path is a compressed archive. `import` unpacks it and
# holds it to the digest the cache gave for the archive.
xz -c "$nar" > "$scratch/thing.nar.xz"
"$script" import xz "$scratch/thing.nar.xz" "$scratch/imported.nar" "$digest"
cmp "$nar" "$scratch/imported.nar"
"$script" import none "$nar" "$scratch/copied.nar" "$digest"
cmp "$nar" "$scratch/copied.nar"

# An archive with another digest is refused and nothing is left behind.
wrong=0000000000000000000000000000000000000000000000000000000000000000
if "$script" import xz "$scratch/thing.nar.xz" "$scratch/refused.nar" "$wrong" 2> "$scratch/refusal"; then
    exit 1
fi
test ! -e "$scratch/refused.nar"
grep -q "$digest" "$scratch/refusal"

# The workspace keeps the archive and its digest in the store path's
# package, and a step's closure says which package that is.
mkdir -p "$scratch/ws/store/$name" "$scratch/ws/actions"
cp "$nar" "$scratch/ws/store/$name/nar"
printf '%s\n' "$digest" > "$scratch/ws/store/$name/nar.sha256"
printf '%s\x1f%s\n' "/nix/store/$name" "store/$name" > "$scratch/ws/closure.txt"
line in dir 700 1 /build '' '' > "$scratch/ws/actions/step.manifest"

# A step unpacks the archive into the script's store under its digest.
# What it unpacks is what Nix keeps: nothing writable, every time stamp 1.
store=$scratch/unpacked
step() {
    GENRULIFY_STORE=$store "$script" --name step --in "$scratch/ws" --out "$scratch/ws-out"
}
step
unpacked=$store/$digest
test "$(sha256sum < "$unpacked/bin/tool" | cut -d' ' -f1)" = "$bundle"
test "$(stat -c %a "$unpacked/bin/tool")" = 555
test "$(stat -c %a "$unpacked/notes")" = 444
test "$(stat -c %a "$unpacked")" = 555
test "$(readlink "$unpacked/latest")" = notes
for entry in "$unpacked" "$unpacked/bin" "$unpacked/bin/tool" "$unpacked/notes" "$unpacked/latest"; do
    test "$(stat -c %Y "$entry")" = 1
done

# A second step finds the path unpacked and does not read the archive
# again, which the test shows by taking the archive away.
rm "$scratch/ws/store/$name/nar"
step
test -e "$unpacked/notes"

# Nothing of an unpacking that is under way is left in the store.
test "$(ls -A "$store" | wc -l)" = 1
