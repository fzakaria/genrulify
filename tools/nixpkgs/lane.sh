#!/usr/bin/env bash
# One lane of the whole-nixpkgs run: takes jobs off a shared list, one
# "ATTR DERIVATION" pair per line, until none is left or a stop file
# appears.
#
# usage: lane.sh JOBS-FILE
#
# MIN_FREE_GIGABYTES  free space on the batch directory's disk below
#                     which no new job is started. The machine is shared.
# MAX_RUNNING         jobs that may run at once, over all lanes
set -u
base=${NIXPKGS_BASE:-$HOME/genrulify-nixpkgs}
here=$(dirname "$0")
min_free=${MIN_FREE_GIGABYTES:-500}
max_running=${MAX_RUNNING:-40}

# How long a lane sleeps before it looks again.
WAIT_SECONDS=30

# Whether fewer jobs are running than the lanes may run between them.
# Lanes that were replaced leave their last job running, and the lanes
# that replace them wait for those jobs and do not double the load.
below_cap() {
    [ "$(pgrep -u "$(id -u)" -fc 'tools/one[.]sh')" -lt "$max_running" ]
}

# Whether the disk has room for another job.
has_room() {
    local free
    free=$(df --output=avail -BG "$base" | tail -1 | tr -dc 0-9)
    [ "$free" -ge "$min_free" ]
}

# Lanes started together would all see room under the cap at once.
STAGGER_SECONDS=60
sleep $((RANDOM % STAGGER_SECONDS))

while read -r attr drv; do
    # A pause file holds every lane between jobs, which is the only time
    # Rewind will collect the images of runs that are gone. A disk that
    # is filling up holds them too.
    while { [ -e "$base/pause" ] || ! has_room || ! below_cap; } && [ ! -e "$base/stop" ]; do
        sleep "$WAIT_SECONDS"
    done
    if [ -e "$base/stop" ]; then
        break
    fi
    "$here/one.sh" "$attr" "$drv" < /dev/null
done < "$1"
