# A recorded build can be emitted as one package of a workspace that many
# builds share, where a store path the build read is a target of the
# workspace and not a path on the host. The test emits the example into
# an empty directory and checks the package's rules, the files every
# package shares, what the package says of itself for `link`, and that
# emitting again replaces the package and nothing else.
set -eu
compiler=$1
data=$2
scratch=$(mktemp -d)
trap 'chmod -R u+w "$scratch"; rm -rf "$scratch"' EXIT
"$compiler" import "$data/example" > "$scratch/graph.mlir"

# The package is named after the store path of the output it builds, and
# emit prints its label.
key=l1dx8g9mr73bdp8y6i2yp92xprmz8xzg-genrulify-example
"$compiler" emit --into "$scratch/mono" "$data/example" "$scratch/graph.mlir" > "$scratch/label"
grep -qx "//pkgs/$key:archive" "$scratch/label"
package=$scratch/mono/pkgs/$key
cd "$package"

# The package has rules for Bazel only, loaded from the shared directory,
# and keeps no copy of the script or of the guest's /bin and /etc.
test -e BUILD.bazel
test ! -e BUCK
test ! -e sandbox.sh
test ! -e platform
grep -q '^load("//genrulify:genrulify.bzl", ' BUILD.bazel
grep -q '^package(default_visibility = \["//visibility:public"\])$' BUILD.bazel

# A rule reads the shared files from their package, and its own from its
# own. The script is told each place a file can be: the package, the
# shared directory, what the package's rules wrote, and for the archives
# of store paths the workspace's sources and everything any rule wrote.
rule() {
    sed -n "/name = \"$1\"/,/^)/p" BUILD.bazel
}
rule s764_dd | grep -q '"//genrulify:sandbox.sh",'
rule s764_dd | grep -q '"//genrulify:platform/etc/passwd",'
rule s764_dd | grep -q '":actions/s764_dd.manifest",'
rule s764_dd | grep -q '":s735_dd/build/bundle",'
rule s764_dd | grep -qF "\"./genrulify/sandbox.sh --in pkgs/$key --in genrulify --in \$(RULEDIR) --in . --in \$(BINDIR) --out \$(RULEDIR)\""

# A rule that runs a command is given the store paths of the closure as
# one target. The archive runs nothing and is given none.
rule s764_dd | grep -q '":closure",'
rule s764_dd | grep -q '":closure.txt",'
if rule archive | grep -q closure; then
    exit 1
fi

# The closure target names a target of the workspace for each store path,
# and closure.txt tells the script where each one's archive is.
closure_size=$(grep -c '' closure.txt)
test "$closure_size" -gt 0
test "$(sed -n '/^filegroup($/,/^)$/p' BUILD.bazel | grep -c '"//store/.*:path",')" = "$closure_size"
first=$(head -n 1 closure.txt)
path=${first%%$'\x1f'*}
test "${first#*$'\x1f'}" = "store/${path#/nix/store/}"
sed -n '/^filegroup($/,/^)$/p' BUILD.bazel | grep -qF "\"//store/${path#/nix/store/}:path\","

# Every output is one target, and `outputs` is all of them.
grep -q 'name = "outputs"' BUILD.bazel

# What the package builds, for `link`: each output's store path with the
# digest its archive must have, and the closure.
grep -q "\"path\": \"/nix/store/$key\"" package.json
grep -Eq '"digest": "[0-9a-f]{64}"' package.json
grep -qF "\"$path\"" package.json

# The shared directory has the script, the rule definitions and the
# guest's files, all exported.
test -x "$scratch/mono/genrulify/sandbox.sh"
test -e "$scratch/mono/genrulify/genrulify.bzl"
test -e "$scratch/mono/genrulify/platform/etc/passwd"
grep -q 'exports_files' "$scratch/mono/genrulify/BUILD.bazel"

# Emitting again replaces the package and leaves its neighbours alone.
mkdir "$scratch/mono/pkgs/neighbour"
touch stale
"$compiler" emit --into "$scratch/mono" "$data/example" "$scratch/graph.mlir" > /dev/null
test ! -e "$package/stale"
test -e "$package/BUILD.bazel"
test -d "$scratch/mono/pkgs/neighbour"
