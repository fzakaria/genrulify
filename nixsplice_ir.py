"""Lower a straight-line GNU dd builder to regular-file SSA values."""

import os
import re
from collections import defaultdict
from dataclasses import dataclass

STORE = "/nix/store/"
BUILD = "/build/"
OUT = '"$out"'
PLAIN = r"[A-Za-z0-9][A-Za-z0-9._+-]*"
STORE_FILE = rf"{STORE}[0-9a-z]{{32}}-{PLAIN}"
COMMAND = re.compile(
    rf"(?P<dd>{STORE_FILE}/bin/dd) if=(?P<src>{STORE_FILE}|{PLAIN}) "
    rf"of=(?P<dst>{PLAIN}|{re.escape(OUT)})(?P<append> oflag=append conv=notrunc)? status=none"
)


class Reject(ValueError):
    """The supplied build falls outside the supported builder language."""


@dataclass(frozen=True)
class Action:
    step: int
    tool: str
    source: str
    inputs: tuple[str, ...]
    path: str
    version: int
    result: str


def plan(drv, events):
    """Cross-check the whole script and bind reads before publishing each result."""
    args = drv["args"]
    output = drv["env"]["out"]
    if (drv["system"] != "x86_64-linux" or set(drv["outputs"]) != {"out"}
            or drv["outputs"]["out"] != {"path": output.removeprefix(STORE)}
            or set(drv["env"]) != {"builder", "name", "out", "system"}
            or not re.fullmatch(STORE_FILE, output)
            or not re.fullmatch(rf"{STORE_FILE}/bin/bash", drv["builder"])
            or len(args) != 3 or args[:2] != ["-eu", "-c"]):
        raise Reject("expected a single-output bash -eu -c derivation")

    # Bind the builder and every child command to the observed exec sequence.
    execs = [e for e in events if e["kind"]["type"] == "exec"]
    if execs and execs[0]["kind"]["filename"] == "/init":
        execs = execs[1:]
    if (not execs or execs[0]["kind"]["filename"] != drv["builder"]
            or execs[0]["kind"]["argv"] != ["bash", *args]):
        raise Reject("recorded builder arguments differ from the derivation")
    lines = args[-1].strip("\n").split("\n")
    if len(lines) != len(execs) - 1:
        raise Reject("expected one recorded dd exec per script line")

    # Group write opens by exec interval. A shell can exec the last child in place.
    sources = {STORE + name for name in drv["inputs"]["srcs"]}
    writes = defaultdict(list)
    for event in events:
        if event["kind"]["type"] == "open":
            writes[event["pid"]].append(event)

    latest = {}
    versions = defaultdict(int)
    actions = []
    for index, (line, event) in enumerate(zip(lines, execs[1:], strict=True)):
        match = COMMAND.fullmatch(line)
        if not match:
            raise Reject(f"unsupported builder command on line {index + 1}")
        dd, src, dst, append = match.groups()
        if (event["kind"]["filename"] != dd
                or event["kind"]["argv"] != line.replace(OUT, output).split(" ")):
            raise Reject(f"exec at step {event['step']} differs from the script")
        if (dst == OUT) != (index == len(lines) - 1) or src == dst:
            raise Reject("expected distinct files and a final write to $out")

        path = output if dst == OUT else BUILD + dst
        flags = os.O_WRONLY | os.O_CREAT | (os.O_APPEND if append else os.O_TRUNC)
        end = execs[index + 2]["step"] if index + 2 < len(execs) else float("inf")
        opened = [(e["kind"]["path"], e["kind"]["flags"]) for e in writes[event["pid"]]
                  if event["step"] <= e["step"] < end]
        if opened != [(path, flags)]:
            raise Reject(f"unexpected write opens at step {event['step']}")

        # A truncation starts fresh. An append to an existing file consumes its bytes.
        source = src if src in sources else latest.get(src)
        if source is None:
            raise Reject(f"no declared source or previous producer for {src}")
        inputs = (latest[dst], source) if append and dst in latest else (source,)
        versions[dst] += 1
        result = f"a{index + 1}"
        actions.append(Action(event["step"], dd, source, inputs, path, versions[dst], result))
        latest[dst] = result

    return actions
