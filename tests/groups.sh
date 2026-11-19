# Commands that cannot be replayed apart are put in one group. The test
# takes the example recording, whose three dd commands ran one after
# another, gives the commands pipes by editing their descriptors, and
# asks the compiler to group the processes again.
set -eu
compiler=$1
data=$2
scratch=$(mktemp -d)
trap 'chmod -R u+w "$scratch"; rm -rf "$scratch"' EXIT

# A copy of the example recording with one descriptor target replaced.
recording() {
    rm -rf "$scratch/$1"
    cp -r "$data/example" "$scratch/$1"
    chmod -R u+w "$scratch/$1"
    sed -i "s|\"target\": \"$2\"|\"target\": \"pipe:[9]\"|" "$scratch/$1/recording.json"
}

# With no pipe between them, commands that ran one after another are
# three groups, one line each, in the order the groups began.
"$compiler" groups "$data/example" > "$scratch/plain"
test "$(wc -l < "$scratch/plain")" = 3
sed -n 1p "$scratch/plain" | grep -q '^735 749 735$'
sed -n 3p "$scratch/plain" | grep -q '^782 796 782$'

# Standard input is opened for reading only. Commands that hold the read
# end of one pipe may each have taken bytes the others did not get, so
# the three are one group.
recording upstream /dev/null
"$compiler" groups "$scratch/upstream" > "$scratch/joined"
test "$(wc -l < "$scratch/joined")" = 1
grep -q '^735 796 735 764 782$' "$scratch/joined"

# Standard output is opened for reading and writing. A pipe of which
# every command holds both ends, as with a job server, has no upstream
# end, and commands that ran one after another stay apart.
recording server /dev/rewind-tty0
"$compiler" groups "$scratch/server" > "$scratch/apart"
test "$(wc -l < "$scratch/apart")" = 3

# Commands that hold such a pipe at the same time are one group. Here the
# first dd is made to exit after the second one started.
sed -i 's|"exit": 749|"exit": 770|' "$scratch/server/recording.json"
"$compiler" groups "$scratch/server" > "$scratch/overlap"
test "$(wc -l < "$scratch/overlap")" = 2
sed -n 1p "$scratch/overlap" | grep -q '^735 778 735 764$'
