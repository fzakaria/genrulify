"""Counts, in a recording, the groups that left the build tree as they
found it, and the processes in them: what a recorder could skip if it
compared the snapshot before a command with the one before the next.

usage: idle.py RECORDING...
"""
import hashlib
import json
import pathlib
import sys


def fingerprint(path):
    """The digest of a state file's bytes: two equal states have one."""
    return hashlib.sha256(path.read_bytes()).hexdigest()


for directory in map(pathlib.Path, sys.argv[1:]):
    recording = json.loads((directory / "recording.json").read_text())
    groups = recording["groups"]
    idle = 0
    idle_processes = 0
    processes = 0
    for group in groups:
        processes += len(group["processes"])
        before = directory / "states" / f"{group['start']}.json"
        after = directory / "states" / f"{group['end']}.json"
        if fingerprint(before) == fingerprint(after):
            idle += 1
            idle_processes += len(group["processes"])
    print(f"{directory.parent.name}: {len(groups)} groups, {idle} idle; "
          f"{processes} processes, {idle_processes} in idle groups")
