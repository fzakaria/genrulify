# A Nix build of a project starts by copying the source tree into /build
# and making the copy writable. The unpack pass reads those two command
# lines, a `cp -r` and a `chmod -R u+w`, and turns each file they leave
# into a copy of the project's own file, so that a compile reads the file
# in the checkout and no rule copies the whole tree on every edit.
#
# The test hand-writes the graph of such a build and gives the pass a
# recording whose commands at the graph's steps are that cp and that
# chmod: the example's recording with its first two dd commands rewritten.
# It then checks what the pass makes of the graph, and that the pass
# leaves alone every command it cannot read as a plain copy.
set -eu
compiler=$1
data=$2
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

# The recording's first command becomes `cp -r --reflink=auto --
# /nix/store/source app` and its second `chmod -R u+w -- app`, both run in
# /build. Each sed line rewrites the first match that is left.
cp -r "$data/example" "$scratch/recording"
chmod -R u+w "$scratch/recording"
json=$scratch/recording/recording.json
for program in cp chmod; do
    sed -i "0,/bin\/dd\",\$/s||bin/$program\",|" "$json"
    sed -i "0,/\"filename\": \(.*\)bin\/dd\"/s||\"filename\": \\1bin/$program\"|" "$json"
done
sed -i '0,/"if=[^"]*header.txt",/s||"-r", "--reflink=auto", "--",|' "$json"
sed -i '0,/"of=bundle",/s||"/nix/store/source",|' "$json"
sed -i '0,/"status=none"/s||"app"|' "$json"
sed -i '0,/"if=[^"]*body.txt",/s||"-R",|' "$json"
sed -i '0,/"of=bundle",/s||"u+w",|' "$json"
sed -i '0,/"oflag=append",/s||"--",|' "$json"
sed -i '0,/"conv=notrunc",/s||"app"|' "$json"
sed -i '0,/"status=none"/{/"status=none"/d}' "$json"

# The graph: a source tree with a C file and an executable script in a
# directory, the copy of the tree, the chmod of the copy, and a command
# that reads both files.
graph() {
    cat <<MLIR
module {
  %0 = splice.literal "sandbox" at 717 -> [#splice.entry<"/build" dir 700 mtime 1>]
  %1:4 = splice.literal "source" at 717 -> [
      #splice.entry<"/nix/store/source" dir 555 mtime 1>,
      #splice.entry<"/nix/store/source/main.c" file 444 mtime 1 "aa">,
      #splice.entry<"/nix/store/source/sub" dir 555 mtime 1>,
      #splice.entry<"/nix/store/source/sub/tool.sh" file 555 mtime 1 "bb">]
  %2:4 = splice.exec "cp" at 735(%0, %1#0, %1#1, %1#2, %1#3) -> [
      #splice.entry<"/build/app" dir 555 mtime 1>,
      #splice.entry<"/build/app/main.c" file 444 mtime 1 "$1">,
      #splice.entry<"/build/app/sub" dir 555 mtime 1>,
      #splice.entry<"/build/app/sub/tool.sh" file 555 mtime 1 "bb">]
  %3:4 = splice.exec "chmod" at 764(%0, %2#0, %2#1, %2#2, %2#3) -> [
      #splice.entry<"/build/app" dir 755 mtime 1>,
      #splice.entry<"/build/app/main.c" file $2 mtime 1 "$1">,
      #splice.entry<"/build/app/sub" dir 755 mtime 1>,
      #splice.entry<"/build/app/sub/tool.sh" file 755 mtime 1 "bb">]
  %4 = splice.exec "dd" at 782(%0, %3#0, %3#1, %3#2, %3#3) -> [#splice.entry<"/out" file 644 mtime 2 "cc">]
  splice.output "out" "/out"(%4)
}
MLIR
}
optimized() {
    "$compiler" opt --recording "$scratch/recording" "$scratch/graph.mlir" "$@" > "$scratch/opt.mlir"
}

# Both commands are gone. The last command reads each file as a copy of
# the project's own file, with the mode the chmod left, and the
# directories as constants.
graph aa 644 > "$scratch/graph.mlir"
optimized
if grep -q 'splice.exec "cp"\|splice.exec "chmod"' "$scratch/opt.mlir"; then
    exit 1
fi
grep -q 'splice.copy "chmod" at 764(%source_717#1) -> <"/build/app/main.c" file 644 ' "$scratch/opt.mlir"
grep -q 'splice.copy "chmod" at 764(%source_717#3) -> <"/build/app/sub/tool.sh" file 755 ' "$scratch/opt.mlir"
sed -n '/splice.literal "chmod" at 764 folded/,/\]/p' "$scratch/opt.mlir" > "$scratch/folded"
grep -q '"/build/app" dir 755' "$scratch/folded"
grep -q '"/build/app/sub" dir 755' "$scratch/folded"
grep -q 'splice.exec "dd" at 782' "$scratch/opt.mlir"

# The pass reads command lines, so without the recording, or left out of
# the passes, the commands stay.
"$compiler" opt "$scratch/graph.mlir" > "$scratch/opt.mlir"
grep -q 'splice.exec "cp" at 735' "$scratch/opt.mlir"
optimized link dce fold merge raise rules
grep -q 'splice.exec "cp" at 735' "$scratch/opt.mlir"
grep -q 'splice.exec "chmod" at 764' "$scratch/opt.mlir"

# That a command is called cp is not enough. A result that does not have
# the bytes of the file at the matching place in the source tree means
# the command line was misread, and the command is kept. Here the copy of
# main.c has other bytes. The chmod of that copy is given no file of the
# project, so the chmod is kept too.
graph zz 644 > "$scratch/graph.mlir"
optimized
grep -q 'splice.exec "cp" at 735' "$scratch/opt.mlir"
grep -q 'splice.exec "chmod" at 764' "$scratch/opt.mlir"

# A chmod that changes who may execute a file decides the bit itself, and
# the copy would take the bit from the project's file. Here main.c comes
# out executable, which `u+w` cannot do, so the chmod is kept. The cp
# before it is still read as a copy.
graph aa 755 > "$scratch/graph.mlir"
optimized
grep -q 'splice.exec "chmod" at 764' "$scratch/opt.mlir"
if grep -q 'splice.exec "cp"' "$scratch/opt.mlir"; then
    exit 1
fi

# A cp that writes over a file which is already there leaves that file's
# mode, unless it was told to preserve the source's. The copy the pass
# would make takes its executable bit from the project's file, which is
# right for a file cp created and wrong for one it overwrote. Here
# /build/app/main.c exists before the cp runs, so the cp stays a command.
# Told to preserve modes, with -p, the same cp is read as a copy again.
cp -r "$scratch/recording" "$scratch/over"
sed -i 's|^\[|[\n {"content": "zz", "kind": "file", "mode": 420, "mtime": 1, "path": "/build/app/main.c"},|' "$scratch/over/states/735.json"
graph aa 644 > "$scratch/graph.mlir"
"$compiler" opt --recording "$scratch/over" "$scratch/graph.mlir" > "$scratch/opt.mlir"
grep -q 'splice.exec "cp" at 735' "$scratch/opt.mlir"
sed -i 's|"-r", "--reflink=auto"|"-pr", "--reflink=auto"|' "$scratch/over/recording.json"
"$compiler" opt --recording "$scratch/over" "$scratch/graph.mlir" > "$scratch/opt.mlir"
if grep -q 'splice.exec "cp"' "$scratch/opt.mlir"; then
    exit 1
fi

# That a program is called cp says nothing about what it does. The pass
# reads a command line only when the program the kernel loaded is the cp
# or the chmod of GNU coreutils, which the Nix store path of the program
# says. Here the recording's cp is a program of the same name from another
# package, and it stays a command although its result has the bytes of its
# input: a tool that uppercases its input leaves an uppercase file as it
# was. The chmod after it is then given no file of the project and stays
# too.
cp -r "$scratch/recording" "$scratch/other"
sed -i '0,/"exe": "[^"]*"/s||"exe": "/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-mytools-1.0/bin/cp"|' "$scratch/other/recording.json"
graph aa 644 > "$scratch/graph.mlir"
"$compiler" opt --recording "$scratch/other" "$scratch/graph.mlir" > "$scratch/opt.mlir"
grep -q 'splice.exec "cp" at 735' "$scratch/opt.mlir"
grep -q 'splice.exec "chmod" at 764' "$scratch/opt.mlir"

# `genrulify unpack PROGRAM ARG...` prints what the pass reads a command
# line as: the trees a cp copies and where to, or the paths a chmod
# changes. Command lines the pass does not read as a plain copy print
# nothing: a cp that drops the mode, one told its target another way, and
# a chmod that sets the executable bits or sets a mode outright.
reads() {
    "$compiler" unpack "$@"
}
coreutils=/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-coreutils-9.11/bin
reads "$coreutils/cp" -pr -- /a /b | grep -q '^cp /a -> /b$'
reads "$coreutils/cp" -a /a /b /c | grep -q '^cp /a /b -> /c$'
reads "$coreutils/cp" --preserve=mode,timestamps /a /b | grep -q '^cp /a -> /b$'
reads "$coreutils/cp" -r /a /b | grep -q '^cp /a -> /b, a file that is there keeps its mode$'
reads "$coreutils/cp" --preserve=timestamps /a /b | grep -q '^cp /a -> /b, a file that is there keeps its mode$'
test -z "$(reads "$coreutils/cp" --no-preserve=mode /a /b)"
test -z "$(reads "$coreutils/cp" -t /b /a)"
test -z "$(reads "$coreutils/cp" /a)"
reads "$coreutils/chmod" u+w,go-w /a /b | grep -q '^chmod /a /b$'
test -z "$(reads "$coreutils/chmod" -R +x /a)"
test -z "$(reads "$coreutils/chmod" u=rw /a)"
test -z "$(reads "$coreutils/chmod" 644 /a)"
test -z "$(reads "$coreutils/install" -m644 /a /b)"

# A program of the same name from anywhere else is not read: one on the
# search path, one in the build tree, one from another package, and the
# cp of a package whose name only contains the word.
test -z "$(reads cp -r /a /b)"
test -z "$(reads /build/app/cp -r /a /b)"
test -z "$(reads /nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-busybox-1.36/bin/cp -r /a /b)"
test -z "$(reads /nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-uutils-coreutils-0.1/bin/cp -r /a /b)"
test -z "$(reads /nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-mytools-1.0/bin/chmod u+w /a)"
