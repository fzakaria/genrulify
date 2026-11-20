# GENRULIFY_JOBS caps the threads the compiler works on, so that several
# conversions can share one machine. The test runs a command on the
# example recording with the variable unset, set to a count, and set to
# values that are not a count, which every command has to refuse before
# doing any work.
set -eu
compiler=$1
data=$2

# Unset, and set to a positive count, the command runs as usual.
"$compiler" groups "$data/example" > /dev/null
GENRULIFY_JOBS=2 "$compiler" groups "$data/example" > /dev/null

# Zero, a negative number, a word and an empty value are not counts.
for jobs in 0 -3 many 4x ""; do
    if GENRULIFY_JOBS=$jobs "$compiler" groups "$data/example" > /dev/null 2> /dev/null; then
        echo "GENRULIFY_JOBS='$jobs' was accepted"
        exit 1
    fi
done

# The refusal names the variable.
GENRULIFY_JOBS=many "$compiler" groups "$data/example" 2>&1 | grep -q 'GENRULIFY_JOBS'
