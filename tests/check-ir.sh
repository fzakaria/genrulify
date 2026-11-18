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
"$compiler" opt "$scratch/ok.mlir" | grep -q 'splice.exec "cc" at 2'

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
