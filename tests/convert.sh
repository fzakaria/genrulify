# `convert` is the whole pipeline behind one command: record a derivation
# in Rewind, check it, optimize it and write the workspace. Rewind needs
# KVM, so the test covers what the command decides before it starts a
# build: that it is in the usage text, and that it will not write into a
# directory that already exists.
set -eu
compiler=$1
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

# With no arguments the command explains itself.
if "$compiler" convert 2> "$scratch/usage"; then
    exit 1
fi
grep -q 'genrulify convert \[--coarse\] \[--open PROGRAMS\] \[--source PROJECT\] \[--into WORKSPACE\] INSTALLABLE DIRECTORY' "$scratch/usage"

# A build becomes a package of a workspace many builds share, or the
# rules of a project converted in place, and not both. The command says
# so before it builds anything.
mkdir "$scratch/both"
if "$compiler" convert --source "$scratch/both" --into "$scratch/shared" nixpkgs#hello "$scratch/out" 2> "$scratch/error"; then
    exit 1
fi
grep -q 'a project converted in place' "$scratch/error"
test ! -e "$scratch/out"
test ! -e "$scratch/shared"

# A project to convert in place has to be a directory.
if "$compiler" convert --source "$scratch/absent" nixpkgs#hello "$scratch/out" 2> "$scratch/error"; then
    exit 1
fi
grep -q 'is not a directory' "$scratch/error"
test ! -e "$scratch/out"

# A project whose genrulify/ directory is somebody's own work is refused
# before anything is built, not after the build has been recorded.
mkdir -p "$scratch/project/genrulify"
printf 'mine\n' > "$scratch/project/genrulify/notes.txt"
if "$compiler" convert --source "$scratch/project" nixpkgs#hello "$scratch/out" 2> "$scratch/error"; then
    exit 1
fi
grep -q 'genrulify was not written by genrulify' "$scratch/error"
test ! -e "$scratch/out"

# An existing destination is left alone, and nothing is built.
mkdir "$scratch/taken"
if "$compiler" convert nixpkgs#hello "$scratch/taken" 2> "$scratch/error"; then
    exit 1
fi
grep -q 'already exists' "$scratch/error"
test -z "$(ls -A "$scratch/taken")"
