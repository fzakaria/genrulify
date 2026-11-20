#!/usr/bin/env bash
# Copies the shared workspace of the whole-nixpkgs run from the machine
# that makes it into a local repository, and writes the repository's
# README from the run's own report. Nothing is pushed.
#
# usage: publish.sh REPOSITORY
#
# NIXPKGS_HOST      the machine the run is on
# NIXPKGS_BASE      the batch directory there, relative to the home directory
set -eu
repository=${1:?usage: publish.sh REPOSITORY}
host=${NIXPKGS_HOST:-fmzakari@leviathan.cymric-daggertooth.ts.net}
base=${NIXPKGS_BASE:-genrulify-nixpkgs}
NIXPKGS_REVISION=b4fd65b198c599cbe814fcb9f42d25d021595ec9
SHORT_REVISION_LENGTH=8
HARNESS_URL=https://github.com/fzakaria/genrulify/tree/nixpkgs-workspace/tools/nixpkgs
mkdir -p "$repository"

# The numbers first, so that they describe the tree that is copied next.
ssh "$host" "$base/tools/report.sh" > "$repository/report.json"

# Bazel's own links and lock file are a build's, not the workspace's.
rsync -a --delete \
    --exclude '/bazel-*' --exclude '/MODULE.bazel.lock' \
    --exclude '/README.md' --exclude '/LICENSE' --exclude '/report.json' \
    --exclude '/.git' --exclude '/.jj' --exclude '/.gitignore' \
    "$host:$base/ws/" "$repository/"
printf 'bazel-*\nMODULE.bazel.lock\n' > "$repository/.gitignore"

# A number with thousands separators.
number() { printf "%'d" "$(jq -r ".$1" "$repository/report.json")"; }

# The store paths the sandbox script runs its own tools from, as the
# continued lines of one command.
tools=$(grep -o '/nix/store/[a-z0-9]\{32\}-[^/"]*' "$repository/genrulify/sandbox.sh" | sort -u |
    sed 's/^/    /; $ ! s/$/ \\/')

cat > "$repository/README.md" <<README
# genrulify-nixpkgs

nixpkgs as one Bazel workspace of \`genrule\`s, converted by
[genrulify](https://github.com/fzakaria/genrulify).

Each package was built once in [Rewind](https://rewindvm.dev), a
deterministic VM, and every command of that build became a rule. A store
path a build read is a target of the workspace: the archive another
package's rules build, or an \`http_file\` of the NAR cache.nixos.org has.

## Status

nixpkgs [${NIXPKGS_REVISION:0:SHORT_REVISION_LENGTH}](https://github.com/NixOS/nixpkgs/commit/$NIXPKGS_REVISION),
x86_64-linux, on $(jq -r .date "$repository/report.json").

| | derivations |
| --- | ---: |
| in nixpkgs' \`ci/eval/outpaths.nix\` | $(number derivations) |
| tried, from the front of a shuffled list | $(number finished) |
| converted | $(number converted) |
| rebuilt by Bazel, every archive as recorded | $(number validated) |
| rebuilt by Bazel, an archive differed | $(number validation_failed) |

| | store paths |
| --- | ---: |
| in the workspace | $(number store_paths) |
| built by a package of the workspace | $(number built_paths) |
| fetched from cache.nixos.org | $(number fetched_paths) |
| kept as an archive in the workspace | $(number kept_paths) |

The $(number packages) packages in the workspace are $(number rules) rules. Of the
converted derivations, $(number fine) were cut at every command and
$(number coarse), which ran more than 400 commands, only at the commands
their builder ran.

In $(number some_differ) of them an output is not the archive
cache.nixos.org has, and what depends on such an output fetches the
cache's. For most of them the cause is one date: stdenv takes
\`SOURCE_DATE_EPOCH\` from the newest source file that is not newer than
the directory above the build directory, which in Rewind's VM is \`/\`
with a modification time of 1, so the date stays at 1980. Built again
by a Rewind whose init gives \`/\` the time on the VM's clock,
$(number redated_match) of $(number redated) match the cache in every output. The packages here
were recorded before that change.

| not converted | derivations |
| --- | ---: |
| the build did not finish in the VM in 15 minutes | $(number build_timed_out) |
| the build failed in the VM | $(number build_failed) |
| snapshots or replays did not finish in 40 minutes | $(number timed_out) |
| an input no cache has | $(number no_inputs) |
| a fetcher, which needs the network | $(number fixed_output) |
| Rewind or genrulify failed | $(number other_failed) |

## Build something

Bazel 7, user namespaces, and the store paths the sandbox script runs
its own tools from:

\`\`\`console
\$ nix-store --realise \\
$tools

\$ bazel build //:guff
\$ sha256sum bazel-bin/pkgs/*-guff-0.1.0/archive.nar
\`\`\`

An attribute of nixpkgs that was converted is a target at the root. Its
archive rule fails unless the NAR has the digest the recording had.
Store paths are unpacked once below \`/var/tmp/genrulify-store-\$UID\`.

## Layout

\`pkgs/NAME/\` is one derivation, named after the store path of its
\`out\`: \`BUILD.bazel\`, a manifest for each rule under \`actions/\`, the
environments under \`env/\`, and \`package.json\`, which says what the
package builds and read. \`store/NAME/\` is one store path, written by
\`genrulify link\`. \`genrulify/\` holds the sandbox script, the rule
definitions and the guest's \`/bin\` and \`/etc\`. \`narinfo/\` is what
cache.nixos.org said of each store path. \`MODULE.bazel\` has one
\`http_file\` for each fetched store path.

## How it is made

\`\`\`console
\$ genrulify convert --skip-idle --into . nixpkgs#guff /tmp/guff
\$ genrulify link .
\`\`\`

Both commands are on genrulify's \`main\`. The batch that runs them over
nixpkgs is [\`tools/nixpkgs/\`]($HARNESS_URL)
on its \`nixpkgs-workspace\` branch.

nixpkgs is MIT, and so is this. Please see [LICENSE](LICENSE).
README
echo "wrote $repository/README.md"
