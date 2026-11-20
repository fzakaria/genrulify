#!/usr/bin/env bash
# Convert the example project in place and prove what a project conversion
# is for: after an edit to one source, both build systems rebuild what
# depends on that source and nothing else.
#
# The script works on a copy of examples/project. It records the build of
# the copy in Rewind, converts the copy, and builds it with Bazel and
# Buck2. Then it changes the greeting in greet.c and builds
# again. In each system the object of greet.c has to be a new file, the
# object of main.c has to be the file the first build left, and the
# program has to print the new greeting. The archive each system then
# builds has to be the one Nix builds from the edited project, which is
# the check that two build systems agreeing with each other cannot give.
# Last, it converts the edited copy again beside a build file written by
# hand, which has to survive and still build.
#
# A second project checks what somebody runs while they work. Its builder
# greps a file of the project and compiles a number into the program by
# what the grep found. After the file changes, the recorded compile
# command is the wrong one, and `bazel run` and `buck2 run` have to fail
# and say to convert again, as the archive does. Handing over the
# program built the old way would be a quiet lie. After converting again
# both have to print the number Nix's own build prints.
#
# usage: tools/validate-project.sh
#
# Set GENRULIFY_COMPILER to a genrulify binary to use it in place of the
# flake's build.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/genrulify-project.XXXXXXXX")
echo "Artifacts: $work"

# Pin every tool to the flake's nixpkgs so a rerun uses the same versions.
compiler=${GENRULIFY_COMPILER:-$(nix build --no-link --print-out-paths "$root")/bin/genrulify}
bazel=$(nix build --inputs-from "$root" --no-link --print-out-paths nixpkgs#bazel_7)/bin/bazel
buck=$(nix build --inputs-from "$root" --no-link --print-out-paths nixpkgs#buck2)/bin/buck2

# The words the example prints, and the words the edits change them to.
RECORDED_GREETING=Hello
EDITED_GREETING=Howdy
CONVERTED_AGAIN_GREETING=Hey

# The copy is the source of a derivation of its own: a flake beside it
# takes this repository's project and gives it the copy as `src`. The
# example in the repository is never written to.
flake=$work/flake
project=$flake/project
mkdir -p "$flake"
cp -r "$root/examples/project" "$project"
chmod -R u+w "$project"
cat > "$flake/flake.nix" <<NIX
{
  inputs.genrulify.url = "path:$root";
  outputs = { self, genrulify }: {
    packages.x86_64-linux.default =
      genrulify.packages.x86_64-linux.project.overrideAttrs (_: { src = ./project; });
  };
}
NIX
"$compiler" convert --source "$project" "path:$flake#default" "$work/recording"
cd "$project"

# Builds the archive with both systems and keeps each one's NAR.
build() {
    "$bazel" --batch build --enable_bzlmod=false //:archive > "$work/bazel.$1.log" 2>&1
    cp bazel-bin/archive.nar "$work/bazel.$1.nar"
    "$buck" build //:archive --out "$work/buck.$1.nar" > "$work/buck.$1.log" 2>&1
}

# The digest of the NAR Nix builds from the project as it is now. Nix is
# given a copy without what the conversion and the build systems wrote
# into the project, none of which the project's own build reads. The
# example's program does not hold its own store path, so its NAR is the
# same wherever Nix puts it.
native_digest() {
    local copy=$work/native.$1
    mkdir -p "$copy"
    cp "$flake/flake.nix" "$copy/"
    if [ -e "$flake/flake.lock" ]; then
        cp "$flake/flake.lock" "$copy/"
    fi
    cp -r "$project" "$copy/project"
    (
        cd "$copy/project"
        rm -rf genrulify buck-out toolchains BUILD.bazel BUCK WORKSPACE MODULE.bazel .buckconfig
        find . -maxdepth 1 -name 'bazel-*' -type l -delete
    )
    local built
    built=$(nix build --no-link --print-out-paths "path:$copy#default")
    nix-store --dump "$built" | sha256sum | cut -d' ' -f1
}

# The file one system has for the target $2, which is the name genrulify
# gave the file a typed rule writes. Bazel keeps the file under bazel-bin
# at the path the generated rules name. Buck2 says where its copy is.
artifact() {
    if [ "$1" = bazel ]; then
        grep -A 2 "name = \"$2\"," genrulify/targets.bzl |
            sed -n 's|.*file = ":\(.*\)",|bazel-bin/\1|p'
    else
        "$buck" build "//:$2" --show-full-output 2> /dev/null | cut -d' ' -f2-
    fi
}

# When each object was last written, in each system.
stamp() {
    for system in bazel buck2; do
        for object in main.o greet.o; do
            echo "$system $object $(stat -L -c %Y.%y "$(artifact "$system" "$object")")"
        done
    done
}

build first
stamp > "$work/first.stamps"
test "$("$bazel" --batch run --enable_bzlmod=false //:greet 2> /dev/null | grep -c "$RECORDED_GREETING")" = 1

# One second, so that a file written again has another time.
sleep 1
sed -i "s/$RECORDED_GREETING/$EDITED_GREETING/" greet.c
build second
stamp > "$work/second.stamps"

# The object of the file that was edited is new in both systems, and the
# object of the other file is the one the first build left.
for system in bazel buck2; do
    if [ "$(grep "^$system greet.o " "$work/first.stamps")" = "$(grep "^$system greet.o " "$work/second.stamps")" ]; then
        echo "$system did not compile greet.c again" >&2
        exit 1
    fi
    if [ "$(grep "^$system main.o " "$work/first.stamps")" != "$(grep "^$system main.o " "$work/second.stamps")" ]; then
        echo "$system compiled main.c again" >&2
        exit 1
    fi
done
"$bazel" --batch run --enable_bzlmod=false //:greet 2> /dev/null | grep -q "$EDITED_GREETING"
"$buck" run //:greet 2> /dev/null | grep -q "$EDITED_GREETING"

# Both archives are what Nix builds from the edited project.
native=$(native_digest second)
for system in bazel buck; do
    generated=$(sha256sum "$work/$system.second.nar" | cut -d' ' -f1)
    if [ "$generated" != "$native" ]; then
        echo "$system archived sha256:$generated after the edit, and Nix builds sha256:$native" >&2
        exit 1
    fi
done

# A build file somebody wrote: it loads the converted rules and adds a
# rule of its own that depends on the program by name.
cat > BUILD.bazel <<'BAZEL'
load(":genrulify/targets.bzl", "genrulify_targets")

genrulify_targets()

genrule(
    name = "greeting",
    srcs = [":greet"],
    outs = ["greeting.txt"],
    cmd = "$(location :greet) > $@",
)
BAZEL
cp BUILD.bazel "$work/BUILD.mine"

# Convert the edited project again. What the build systems left in the
# directory is not part of the project.
"$buck" kill > /dev/null 2>&1 || true
rm -rf buck-out
find . -maxdepth 1 -name 'bazel-*' -type l -delete
sed -i "s/$EDITED_GREETING/$CONVERTED_AGAIN_GREETING/" greet.c
"$compiler" convert --source "$project" "path:$flake#default" "$work/recording-2"
cmp BUILD.bazel "$work/BUILD.mine"
"$bazel" --batch build --enable_bzlmod=false //:greeting > "$work/bazel.third.log" 2>&1
grep -q "$CONVERTED_AGAIN_GREETING" bazel-bin/greeting.txt
"$buck" run //:greet 2> /dev/null | grep -q "$CONVERTED_AGAIN_GREETING"
"$buck" kill > /dev/null 2>&1 || true

echo "Both systems rebuilt greet.o, kept main.o, archived what Nix builds from the edited project (sha256:$native), and built the hand-written rule after converting again."

# The project whose builder decides by a grep. The number the program
# prints is 1 while the file says the recorded word and 2 otherwise.
RECORDED_WORD=disabled
EDITED_WORD=enabled
RECORDED_NUMBER=1
EDITED_NUMBER=2
CONVERT_AGAIN="convert the project again"
decided=$work/decided
mkdir -p "$decided/project"
echo "$RECORDED_WORD" > "$decided/project/flag"
cat > "$decided/project/main.c" <<'C'
#include <stdio.h>
int main(void) { printf("%d\n", VALUE); return 0; }
C
cat > "$decided/flake.nix" <<NIX
{
  inputs.genrulify.url = "path:$root";
  outputs = { self, genrulify }:
    let pkgs = genrulify.inputs.nixpkgs.legacyPackages.x86_64-linux; in {
      packages.x86_64-linux.default = pkgs.stdenv.mkDerivation {
        pname = "decided";
        version = "0.1.0";
        src = ./project;
        dontConfigure = true;
        dontFixup = true;
        buildPhase = "if grep -q $RECORDED_WORD flag; then n=$RECORDED_NUMBER; else n=$EDITED_NUMBER; fi; gcc -DVALUE=\$n main.c -o app";
        installPhase = "mkdir -p \$out/bin; cp app \$out/bin/app";
      };
    };
}
NIX
"$compiler" convert --source "$decided/project" "path:$decided#default" "$work/decided-recording"
cd "$decided/project"

# Runs the program with one system. Standard output is the program's.
run_app() {
    if [ "$1" = bazel ]; then
        "$bazel" --batch run --enable_bzlmod=false //:app
    else
        "$buck" run //:app
    fi
}
for system in bazel buck2; do
    test "$(run_app "$system" 2> /dev/null)" = "$RECORDED_NUMBER"
done

# The grep now ends another way, so the compile the rules hold is not the
# one the builder would run. Neither system may hand over a program.
echo "$EDITED_WORD" > flag
for system in bazel buck2; do
    if run_app "$system" > "$work/decided.$system.out" 2> "$work/decided.$system.log"; then
        echo "$system ran a program built on a decision that has changed: $(cat "$work/decided.$system.out")" >&2
        exit 1
    fi
    grep -q "$CONVERT_AGAIN" "$work/decided.$system.log"
done

# Converted again, both print what Nix's build of the project prints.
"$buck" kill > /dev/null 2>&1 || true
rm -rf buck-out
find . -maxdepth 1 -name 'bazel-*' -type l -delete
"$compiler" convert --source "$decided/project" "path:$decided#default" "$work/decided-recording-2"
for system in bazel buck2; do
    test "$(run_app "$system" 2> /dev/null)" = "$EDITED_NUMBER"
done
"$buck" kill > /dev/null 2>&1 || true
echo "Both systems refused to run a program whose build decision had changed, and ran the new one after converting again."
