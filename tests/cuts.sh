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
