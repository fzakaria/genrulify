# Trace analysis finds where a build can be split. The fixture is the event
# stream and trace.bin of the example run, in which bash starts two dd
# children and then replaces itself with a third dd. The test checks the
# three cuts, their order, and the read the I/O log attributes to the last.
set -eu
compiler=$1
data=$2
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
"$compiler" cuts "$data/trace/events.jsonl" "$data/trace/trace.bin" > "$scratch/cuts"

# One line per cut, in the order the cuts began.
test "$(wc -l < "$scratch/cuts")" = 3
test "$(grep -c '"name":"dd"' "$scratch/cuts")" = 3
sed -n 1p "$scratch/cuts" | grep -q '"step":735'
sed -n 2p "$scratch/cuts" | grep -q '"step":764'

# The builder's own pid becomes a cut when bash execs its last command, and
# that dd reads the file the first two wrote.
sed -n 3p "$scratch/cuts" | grep -q '"pid":34'
sed -n 3p "$scratch/cuts" | grep -q '"/build/bundle"'

# The first dd copies a store file and reads nothing the build wrote.
if sed -n 1p "$scratch/cuts" | grep -q '"/build/bundle"'; then
    exit 1
fi

# Opening a program makes its processes driver code, so their children are
# cut instead. With dd opened the two dd children are never cut, and the
# builder, which becomes dd by execve, never leaves the driver either.
"$compiler" cuts "$data/trace/events.jsonl" "$data/trace/trace.bin" 'dd' > "$scratch/opened"
test "$(wc -l < "$scratch/opened")" = 0

# A pattern must match the whole program name.
"$compiler" cuts "$data/trace/events.jsonl" "$data/trace/trace.bin" 'd' > "$scratch/partial"
test "$(wc -l < "$scratch/partial")" = 3

# Opening a program also opens whatever had to be a driver to reach it.
# The synthetic trace is a builder that runs `sh -c`, which runs `make`,
# which runs one recipe shell that runs `cc`. Opening make must open the
# outer sh, because make is below it, and must leave the recipe shell as
# the cut, because nothing below the recipe shell is opened.
event() {
    printf '{"step":%s,"pid":%s,"tid":%s,"kind":%s}\n' "$1" "$2" "$2" "$3"
}
{
    event 1 2 '{"type":"exec","filename":"/bin/bash","argv":[]}'
    event 2 2 '{"type":"fork","child":3,"thread":false}'
    event 3 3 '{"type":"exec","filename":"/bin/sh","argv":[]}'
    event 4 3 '{"type":"fork","child":4,"thread":false}'
    event 5 4 '{"type":"exec","filename":"/bin/make","argv":[]}'
    event 6 4 '{"type":"fork","child":5,"thread":false}'
    event 7 5 '{"type":"exec","filename":"/bin/sh","argv":[]}'
    event 8 5 '{"type":"fork","child":6,"thread":false}'
    event 9 6 '{"type":"exec","filename":"/bin/cc","argv":[]}'
    for pid in 6 5 4 3 2; do
        event $((16 - pid)) "$pid" '{"type":"exit","status":0,"thread":false}'
    done
} > "$scratch/events"
printf 'rwtrace\0\1\0\0\0' > "$scratch/trace"

# With coarse cuts and nothing opened, the outer shell is the only cut.
"$compiler" cuts --coarse "$scratch/events" "$scratch/trace" > "$scratch/whole"
test "$(wc -l < "$scratch/whole")" = 1
grep -q '"pid":3' "$scratch/whole"

# By default the cuts are fine: a process that runs commands and writes no
# file itself is a driver. Nothing here writes, so both shells and make are
# drivers and the compiler is the only cut.
"$compiler" cuts "$scratch/events" "$scratch/trace" > "$scratch/fine"
test "$(wc -l < "$scratch/fine")" = 1
grep -q '"pid":6' "$scratch/fine"
grep -q '"name":"cc"' "$scratch/fine"

# The event of a process opening /build/x for writing: O_WRONLY, O_CREAT
# and O_TRUNC.
written='{"type":"open","path":"/build/x","flags":577}'

# A process that writes a file itself did work of its own, so the process
# is a cut. Here make opens a file at step 6, after it started.
{ cat "$scratch/events"; event 6 4 "$written"; } > "$scratch/writer"
"$compiler" cuts "$scratch/writer" "$scratch/trace" > "$scratch/wrote"
test "$(wc -l < "$scratch/wrote")" = 1
grep -q '"pid":4' "$scratch/wrote"

# The same goes for a process that renames a file.
{ cat "$scratch/events"; event 6 4 '{"type":"rename","from":"/build/x","to":"/build/y"}'; } > "$scratch/renamer"
"$compiler" cuts "$scratch/renamer" "$scratch/trace" > "$scratch/renamed"
grep -q '"pid":4' "$scratch/renamed"

# Opening a device changes no file of the build.
{ cat "$scratch/events"; event 6 4 '{"type":"open","path":"/dev/null","flags":577}'; } > "$scratch/device"
"$compiler" cuts "$scratch/device" "$scratch/trace" > "$scratch/discarded"
grep -q '"pid":6' "$scratch/discarded"

# What a child opens between its fork and its execve is the shell setting
# up a redirection for the command, and counts against neither the parent
# nor the command. The recipe shell is forked at step 6 and execs at 7.
{ cat "$scratch/events"; event 6 5 "$written"; } > "$scratch/redirect"
"$compiler" cuts "$scratch/redirect" "$scratch/trace" > "$scratch/redirected"
test "$(wc -l < "$scratch/redirected")" = 1
grep -q '"pid":6' "$scratch/redirected"

# A cut says which process, opened on the evidence of the trace, started
# it: here the recipe shell, pid 5, which called execve at step 7. A driver
# can be closed again by naming that process, and then the driver is the
# cut. The recorder does this for a driver that fed a command through a
# pipe, because what a builtin wrote into a pipe is in no file.
grep -q '"opener":{"pid":5,"step":7}' "$scratch/fine"
"$compiler" cuts --close 5@7 "$scratch/events" "$scratch/trace" > "$scratch/closed"
test "$(wc -l < "$scratch/closed")" = 1
grep -q '"pid":5' "$scratch/closed"
grep -q '"opener":{"pid":4,"step":5}' "$scratch/closed"

# With coarse cuts and make opened, the recipe shell is the only cut and it
# spans the compiler.
"$compiler" cuts --coarse "$scratch/events" "$scratch/trace" make > "$scratch/recipe"
test "$(wc -l < "$scratch/recipe")" = 1
grep -q '"pid":5' "$scratch/recipe"
grep -q '"/bin/cc"' "$scratch/recipe"

# A shell given one command replaces itself with the command instead of
# forking it. Here the outer sh becomes make by execve, so opening make
# has to open the sh that turned into it.
{
    event 1 2 '{"type":"exec","filename":"/bin/bash","argv":[]}'
    event 2 2 '{"type":"fork","child":3,"thread":false}'
    event 3 3 '{"type":"exec","filename":"/bin/sh","argv":[]}'
    event 4 3 '{"type":"exec","filename":"/bin/make","argv":[]}'
    event 5 3 '{"type":"fork","child":4,"thread":false}'
    event 6 4 '{"type":"exec","filename":"/bin/cc","argv":[]}'
    for pid in 4 3 2; do
        event $((12 - pid)) "$pid" '{"type":"exit","status":0,"thread":false}'
    done
} > "$scratch/events"
"$compiler" cuts --coarse "$scratch/events" "$scratch/trace" make > "$scratch/replaced"
test "$(wc -l < "$scratch/replaced")" = 1
grep -q '"pid":4' "$scratch/replaced"

# Only shells are opened on account of what they run. A configure script
# also runs make, to test it, and must stay one cut.
sed 's|/bin/sh|/build/configure|' "$scratch/events" > "$scratch/configure-events"
"$compiler" cuts --coarse "$scratch/configure-events" "$scratch/trace" make > "$scratch/script"
test "$(wc -l < "$scratch/script")" = 1
grep -q '"pid":3' "$scratch/script"
grep -q '"name":"configure"' "$scratch/script"
