# Raising gives a recorded command the name of the kind of build rule the
# command stands for. `genrulify rule PROGRAM ARG...` prints the kind of a
# command line, or nothing when the command has none, and the test hands
# it compiles, links, archives and commands that are none of those. The
# test then raises a recording whose middle command was turned into a
# compile, and checks the kind in the IR, in the description and in the
# build files, where the rule names its source and its object.
set -eu
compiler=$1
data=$2
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

# Fails unless the command line after $1 is raised to the kind $1.
raises() {
    expected=$1
    shift
    actual=$("$compiler" rule "$@")
    if [ "$actual" != "$expected" ]; then
        echo "$*: expected '$expected', found '$actual'" >&2
        exit 1
    fi
}

# A C compiler driver that stops after compiling writes an object. One that
# links writes a program, or a shared library when the output is named
# like one or the command asks for one. A driver that only preprocesses or
# prints something is no rule.
raises cc_object /nix/store/x-gcc-16/bin/gcc -O2 -c -o a.o a.c
raises cc_object x86_64-unknown-linux-gnu-g++ -c a.cc
raises cc_object clang++ -std=c++20 -c -oa.o a.cc
raises cc_binary cc -o prog a.o b.o -lm
raises cc_binary c++ a.o -o /out/bin/prog
raises cc_shared_library gcc -o libx.so.1.2 a.o
raises cc_shared_library clang -shared -o plugin a.o
raises '' gcc -E a.c -o a.i
raises '' gcc -E -c a.c -o a.i
raises '' gcc -c -S a.c
raises '' gcc -S a.c
raises '' gcc --version
raises '' gcc a.c

# ar builds a static library when it adds members, and not when it lists
# or extracts them. ranlib only indexes one.
raises cc_library ar rc libx.a a.o b.o
raises cc_library /bin/ar -cqs libx.a a.o
raises '' ar t libx.a
raises '' ar x libx.a
raises '' ranlib libx.a

# rustc says what it builds with --crate-type.
raises rust_library rustc --crate-name x --crate-type lib src/lib.rs
raises rust_library rustc --crate-type=rlib src/lib.rs
raises rust_binary rustc --crate-name x --crate-type bin src/main.rs
raises rust_proc_macro rustc --crate-type proc-macro src/lib.rs
raises '' rustc -vV

# Anything else keeps the plain rule.
raises '' dd if=a of=b
raises '' sed -e s/a/b/ file

# A script is run by its interpreter, which the kernel puts in front of the
# script's own arguments. The fixture's recording has such a process once
# its dd commands are renamed, so the pass is tested on a recording: the
# example with /build/bundle renamed to a C source and the appending dd
# turned into a compile of that source.
cp -r "$data/example" "$scratch/recording"
chmod -R u+w "$scratch/recording"
sed -i 's|/build/bundle|/build/bundle.c|g' "$scratch/recording/recording.json" "$scratch/recording"/states/*.json
sed -i 's|bin/dd"|bin/gcc"|; s|"oflag=append",|"-c",|; s|"conv=notrunc",|"bundle.c",|' "$scratch/recording/recording.json"
"$compiler" import "$scratch/recording" > "$scratch/graph.mlir"

# Without the recording the pass has no command lines to look at and tags
# nothing. With it, the compile is tagged and the other two are not.
"$compiler" opt "$scratch/graph.mlir" > "$scratch/plain.mlir"
if grep -q ' as "' "$scratch/plain.mlir"; then
    exit 1
fi
"$compiler" opt --recording "$scratch/recording" "$scratch/graph.mlir" > "$scratch/raised.mlir"
grep -q '%dd_764 = splice.exec "dd" at 764 as "cc_object"(%sandbox_717, %dd_735)' "$scratch/raised.mlir"
test "$(grep -c ' as "' "$scratch/raised.mlir")" = 1

# The pass can be left out, and what is printed parses again.
"$compiler" opt --recording "$scratch/recording" "$scratch/graph.mlir" dce > "$scratch/untagged.mlir"
if grep -q ' as "' "$scratch/untagged.mlir"; then
    exit 1
fi
"$compiler" opt "$scratch/raised.mlir" dce | grep -q 'at 764 as "cc_object"'

# The description says which kind a node has.
"$compiler" describe "$scratch/recording" "$scratch/graph.mlir" > "$scratch/description.json"
grep -q '"rule":"cc_object"' "$scratch/description.json"

# The build files call the rules that ship in the workspace. The compile
# names its source apart from the files that only have to be there, and
# its object. The command is the one a plain rule would run.
"$compiler" emit "$scratch/recording" "$scratch/raised.mlir" "$scratch/workspace"
cd "$scratch/workspace"
test -s genrulify.bzl
test -s genrulify.buck.bzl
grep -q '^load(":genrulify.bzl", "built_file", "cc_object", "nix_archive", "recorded")$' BUILD.bazel
grep -q '^load(":genrulify.buck.bzl", "built_file", "cc_object", "nix_archive", "recorded")$' BUCK
# Prints the rule named $2 of the build file $1, from the line that says
# which rule it is to its closing parenthesis.
rule() {
    awk -v name="    name = \"$2\"," '
        /^[a-z_]*\($/ { head = $0; named = 1; next }
        named { named = 0; if ($0 == name) { print head; shown = 1 } }
        shown { print }
        shown && /^\)$/ { shown = 0 }
    ' "$1"
}
rule BUILD.bazel s764_dd | grep -q '^cc_object($'
rule BUILD.bazel s764_dd | grep -q 'src = ":s735_dd/build/bundle.c",'
rule BUILD.bazel s764_dd | grep -q 'out = "s764_dd/build/bundle.c",'
rule BUILD.bazel s764_dd | grep -q '":sandbox.sh",'
rule BUILD.bazel s764_dd | grep -q 'if=.*-body.txt of=bundle -c bundle.c status=none'
rule BUILD.bazel s735_dd | grep -q '^recorded($'
rule BUILD.bazel archive | grep -q '^nix_archive($'
rule BUCK s764_dd | grep -q '^cc_object($'
rule BUCK s764_dd | grep -q 'src = ":s735_dd\[build/bundle.c\]",'
rule BUCK s764_dd | grep -q 'out = "s764_dd/build/bundle.c",'

# A rule is named after the step it was recorded at, which changes from
# one recording to the next. The file a typed rule writes gets a target of
# its own, named after the file's place in the build tree, that a rule
# someone wrote by hand can depend on and that stays put when the project
# is converted again.
rule BUILD.bazel bundle.c | grep -q '^built_file($'
rule BUILD.bazel bundle.c | grep -q 'file = ":s764_dd/build/bundle.c",'
rule BUCK bundle.c | grep -q 'file = ":s764_dd\[build/bundle.c\]",'

# A link's file is a program, which `bazel run` and `buck2 run` start.
# Here the compile is turned into a link by giving it `-o`. Two rules
# that write the same path are two versions of one file, and the name
# goes to the later one: the first dd writes the path the link then
# rewrites, and is no typed rule, so only the link is named.
sed -i 's|"-c",|"-o",|' "$scratch/recording/recording.json"
"$compiler" opt --recording "$scratch/recording" "$scratch/graph.mlir" > "$scratch/linked.mlir"
"$compiler" emit "$scratch/recording" "$scratch/linked.mlir" "$scratch/linked"
cd "$scratch/linked"
rule BUILD.bazel s764_dd | grep -q '^cc_binary($'
rule BUILD.bazel bundle.c | grep -q '^program($'
rule BUILD.bazel bundle.c | grep -q 'file = ":s764_dd/build/bundle.c",'
rule BUCK bundle.c | grep -q '^program($'
test "$(grep -c 'name = "bundle.c"' BUILD.bazel)" = 1
grep -q '^program = rule($' genrulify.bzl
grep -q 'RunInfo' genrulify.buck.bzl
if rule BUILD.bazel bundle.c | grep -q 'libs'; then
    exit 1
fi

# A program can load a shared library its own project built, and finds
# the library beside itself where the build left both. In a build
# system's tree the two are files of different rules in different
# places. So the program's target is told the shared libraries the link
# was given, each with the name the link knew it by, and puts them where
# the loader looks when the program is run. Here the link reads a file
# called libbundle.so that the first dd wrote.
cp -r "$scratch/recording" "$scratch/shared"
sed -i 's|/build/bundle\.c|/build/libbundle.so|g' "$scratch/shared/recording.json" "$scratch/shared"/states/*.json
"$compiler" import "$scratch/shared" > "$scratch/shared.mlir"
"$compiler" opt --recording "$scratch/shared" "$scratch/shared.mlir" > "$scratch/shared.opt.mlir"
"$compiler" emit "$scratch/shared" "$scratch/shared.opt.mlir" "$scratch/shared-workspace"
cd "$scratch/shared-workspace"
rule BUILD.bazel libbundle.so | grep -q '^program($'
rule BUILD.bazel libbundle.so | grep -A 2 'libs = \[' | grep -q '":s735_dd/build/libbundle.so",'
rule BUILD.bazel libbundle.so | grep -A 2 'lib_names = \[' | grep -q '"libbundle.so",'
rule BUCK libbundle.so | grep -A 2 'libs = \[' | grep -q '":s735_dd\[build/libbundle.so\]",'
grep -q 'LD_LIBRARY_PATH' genrulify.bzl
grep -q 'LD_LIBRARY_PATH' genrulify.buck.bzl

# A rule written by hand that lists the program among its sources is
# given the program and not the script `bazel run` starts it through,
# which only works where its runfiles are.
grep -q 'files = depset(\[ctx.file.file\]),' genrulify.bzl
cd "$scratch/workspace"

# A rule in the shipped files runs the recorded command and nothing else,
# so a kind cannot change what is run. Bazel's rules are all one genrule.
# Buck2's are all one rule of the file's own, which every kind calls
# through the same function.
grep -q 'native.genrule(' genrulify.bzl
test "$(grep -c '^    _run(' genrulify.buck.bzl)" = 8
test "$(grep -c '    _step(' genrulify.buck.bzl)" = 2
