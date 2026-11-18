# The dialect's verifiers reject graphs no recording could produce. Each
# case is a small module handed to `opt`, which must fail with a message
# naming the defect.
set -eu
compiler=$1
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

# Runs `opt` on standard input and requires a diagnostic matching $1.
rejects() {
    cat > "$scratch/bad.mlir"
    if "$compiler" opt "$scratch/bad.mlir" > /dev/null 2> "$scratch/error"; then
        echo "accepted a module that should fail: $1" >&2
        exit 1
    fi
    grep -q "$1" "$scratch/error"
}

# A well-formed module parses and prints.
cat > "$scratch/ok.mlir" <<'MLIR'
module {
  %0 = splice.literal "sandbox" at 1 -> [#splice.entry<"/build" dir 700 mtime 1>]
  %1 = splice.exec "cc" at 2(%0) -> [#splice.entry<"/build/a.out" file 755 mtime 1 "aa">]
  splice.output "out" "/build"(%0, %1)
}
MLIR
"$compiler" opt "$scratch/ok.mlir" dce > "$scratch/ok.out"
grep -q 'splice.exec "cc" at 2' "$scratch/ok.out"

# A value is named after the operation that defines it and the step the
# operation ran at, so the name survives a pass that removes other
# operations, and a reader can tell what `%cc_2` is.
grep -q '%cc_2 = splice.exec "cc" at 2(%sandbox_1)' "$scratch/ok.out"
grep -q 'splice.output "out" "/build"(%sandbox_1, %cc_2)' "$scratch/ok.out"

# A name is made of the characters MLIR allows in one.
sed 's/"cc"/"find|sort"/' "$scratch/ok.mlir" > "$scratch/pipeline.mlir"
"$compiler" opt "$scratch/pipeline.mlir" dce | grep -q '%find_sort_2 = splice.exec "find|sort" at 2'

# A process group can carry the kind of build rule it stands for, and the
# kind is printed where it was written.
sed 's/splice.exec "cc" at 2(/splice.exec "cc" at 2 as "cc_object"(/' "$scratch/ok.mlir" > "$scratch/kind.mlir"
"$compiler" opt "$scratch/kind.mlir" dce | grep -q '%cc_2 = splice.exec "cc" at 2 as "cc_object"(%sandbox_1)'

# A kind is a name, so an empty one is rejected.
sed 's/ as "cc_object"/ as ""/' "$scratch/kind.mlir" > "$scratch/empty.mlir"
if "$compiler" opt "$scratch/empty.mlir" dce > /dev/null 2> "$scratch/error"; then
    exit 1
fi
grep -q 'empty rule kind' "$scratch/error"

# A constant folded out of a process group and a copy of a file both parse
# and print.
cat > "$scratch/raised.mlir" <<'MLIR'
module {
  %0 = splice.literal "sandbox" at 1 -> [#splice.entry<"/build/f" file 644 mtime 1 "aa">]
  %1 = splice.literal "mkdir" at 2 folded -> [#splice.entry<"/out" dir 755 mtime 2>]
  %2 = splice.copy "cp" at 3(%0) -> #splice.entry<"/out/f" file 755 mtime 3 "aa">
  splice.output "out" "/out"(%1, %2)
}
MLIR
"$compiler" opt "$scratch/raised.mlir" dce > "$scratch/raised.out"
grep -q 'splice.literal "mkdir" at 2 folded' "$scratch/raised.out"
grep -q '%cp_3 = splice.copy "cp" at 3(%sandbox_1) -> <"/out/f" file 755 mtime 3 "aa">' "$scratch/raised.out"

# What is printed parses again.
"$compiler" opt "$scratch/raised.out" dce | grep -q 'splice.copy "cp" at 3(%sandbox_1)'


# A copy has the bytes of its source, and only a regular file has bytes.
rejects 'content of its source' <<'MLIR'
module {
  %0 = splice.literal "sandbox" at 1 -> [#splice.entry<"/build/f" file 644 mtime 1 "aa">]
  %1 = splice.copy "cp" at 2(%0) -> #splice.entry<"/build/g" file 644 mtime 2 "bb">
}
MLIR
rejects 'regular file' <<'MLIR'
module {
  %0 = splice.literal "sandbox" at 1 -> [#splice.entry<"/build" dir 700 mtime 1>]
  %1 = splice.copy "cp" at 2(%0) -> #splice.entry<"/out" dir 700 mtime 2>
}
MLIR

# A process group sees one filesystem and cannot read two versions of a path.
rejects 'two versions' <<'MLIR'
module {
  %0 = splice.literal "sandbox" at 1 -> [#splice.entry<"/build/f" file 644 mtime 1 "aa">]
  %1 = splice.literal "glue" at 2 -> [#splice.entry<"/build/f" file 644 mtime 1 "bb">]
  %2 = splice.exec "cat" at 3(%0, %1) -> [#splice.entry<"/build/g" file 644 mtime 1 "cc">]
}
MLIR

# An operation cannot write the same path twice.
rejects 'twice' <<'MLIR'
module {
  %0:2 = splice.literal "glue" at 1 -> [
    #splice.entry<"/build/f" file 644 mtime 1 "aa">,
    #splice.entry<"/build/f" file 644 mtime 1 "bb">]
}
MLIR

# An output only publishes entries below its own path.
rejects 'outside' <<'MLIR'
module {
  %0 = splice.literal "glue" at 1 -> [#splice.entry<"/build/f" file 644 mtime 1 "aa">]
  splice.output "out" "/nix/store/x"(%0)
}
MLIR

# An entry has a known kind and an octal mode.
rejects 'octal mode' <<'MLIR'
module {
  %0 = splice.literal "glue" at 1 -> [#splice.entry<"/build/f" socket 644 mtime 1>]
}
MLIR
rejects 'octal mode' <<'MLIR'
module {
  %0 = splice.literal "glue" at 1 -> [#splice.entry<"/build/f" file 689 mtime 1 "aa">]
}
MLIR
