import json
import unittest
from pathlib import Path

from nixsplice_ir import Reject, plan

DATA = Path(__file__).parent / "data"


class PlanTest(unittest.TestCase):
    """Compile the recorded example and corrupt its evidence one field at a time."""

    def setUp(self):
        self.drv = next(iter(json.loads((DATA / "derivation.json").read_text())["derivations"].values()))
        self.events = [json.loads(row) for row in (DATA / "events.jsonl").read_text().splitlines()]

    def test_append(self):
        """Check the append consumes the old bundle as well as the second source."""
        graph = plan(self.drv, self.events)
        first, append, final = graph
        self.assertEqual(append.inputs, (first.result, append.source))
        self.assertEqual(final.inputs, (append.result,))
        self.assertEqual([first.version, append.version], [1, 2])

    def test_missing_exec(self):
        """Drop a child exec and require the script-to-trace check to fail."""
        self.events = [e for e in self.events if e["step"] != 764]
        with self.assertRaises(Reject):
            plan(self.drv, self.events)

    def test_parent_write(self):
        """Append a shell builtin to both script copies and reject the unsupported language."""
        self.drv["args"][-1] += 'echo bad >> "$out"\n'
        builder = next(e["kind"] for e in self.events if e["kind"].get("filename") == self.drv["builder"])
        builder["argv"][-1] = self.drv["args"][-1]
        with self.assertRaises(Reject):
            plan(self.drv, self.events)

    def test_missing_write(self):
        """Drop a write-open to check the output path against the guest's filesystem."""
        events = [e for e in self.events if e["step"] != 746]
        with self.assertRaises(Reject):
            plan(self.drv, events)
