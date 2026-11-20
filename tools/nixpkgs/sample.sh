#!/usr/bin/env bash
# Samples what the conversions are running: counts, over a few seconds,
# the kinds of Rewind query and the other helpers alive at each instant.
SAMPLES=40
INTERVAL=0.25
for _ in $(seq "$SAMPLES"); do
    ps -u "$(id -u)" -o args= | awk '
        /rewind --progress off cat --archive/ {print "archive"; next}
        /rewind --progress off cat/ {print "cat"; next}
        /rewind --progress off files/ {print "files"; next}
        /rewind --progress off events/ {print "events"; next}
        /rewind nix/ {print "rewind-nix"; next}
        /bin\/tar --extract/ {print "tar"; next}
        /genrulify record/ {print "genrulify-record"; next}
        /genrulify check/ {print "genrulify-check"; next}
        /sandbox.sh/ {print "sandbox"; next}'
    sleep "$INTERVAL"
done | sort | uniq -c | sort -rn
echo "per-thread cpu of genrulify record:"
ps -u "$(id -u)" -o pcpu=,nlwp=,etimes=,args= | grep 'genrulify record' | grep -v grep | awk '{print $1, $2, $3}' | head -20
