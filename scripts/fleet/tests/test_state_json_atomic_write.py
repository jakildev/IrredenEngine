"""Tests for the atomic cache write + tolerant reader in fleet-state-scout (#1750).

The bug: `write_atomic` used a FIXED temp name (`<name>.tmp`), so two concurrent
writers on the same target (the 30s fleet-up scout tick racing a manual /
solo-architect scout run, or an overlong tick) shared one temp — writer B could
os.replace a file writer A was still mid-write into, exposing a truncated splice
to a reader's json.loads. The fix gives each call a unique temp via
`tempfile.mkstemp(dir=path.parent)` + fsync + os.replace.

Covers:
  - write_atomic round-trips and leaves no temp behind (success + error paths);
  - the cross-PROCESS race: many writers hammering one target with distinct
    complete payloads never expose a partial/spliced file to a raw reader;
  - read_json_retry returns on valid input, recovers when the file becomes valid
    mid-retry, and raises the last error (not a spurious one) when it never does.

The race is cross-process, not cross-thread — Python's GIL serialises enough of a
write_text that threads rarely interleave the way processes do (see the plan's
gotcha). So the concurrency case uses subprocess writers, each loading the real
write_atomic from the scout, with a large payload + many iterations tuned to
exercise the interleave window the shared-temp bug exposed.
"""
import glob
import importlib.machinery
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"


def _load_scout():
    loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
    spec = importlib.util.spec_from_loader("fleet_state_scout", loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


_mod = _load_scout()
write_atomic = _mod.write_atomic
read_json_retry = _mod.read_json_retry

# A subprocess writer that drives the REAL write_atomic (loaded from the scout)
# in a tight loop with one writer-unique, complete payload. argv:
#   <scout-path> <target> <writer-id> <iters> <blob-bytes>
_WRITER_SRC = r"""
import importlib.machinery, importlib.util, json, sys
from pathlib import Path

scout_path, target, wid, iters, blob_bytes = (
    sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]))
loader = importlib.machinery.SourceFileLoader("fleet_state_scout", scout_path)
spec = importlib.util.spec_from_loader("fleet_state_scout", loader)
mod = importlib.util.module_from_spec(spec)
loader.exec_module(mod)

# Distinct, complete payload for this writer. The blob makes the write big
# enough to actually interleave; a spliced read is either invalid JSON or a
# wrong/short blob, both of which the parent detects.
payload = json.dumps(
    {"writer": wid, "blob": str(wid) * blob_bytes}, sort_keys=True) + "\n"
for _ in range(iters):
    mod.write_atomic(Path(target), payload)
"""


def _expected_payload(wid, blob_bytes):
    return json.dumps(
        {"writer": wid, "blob": str(wid) * blob_bytes}, sort_keys=True) + "\n"


class TestWriteAtomic(unittest.TestCase):
    def setUp(self):
        self._dir = tempfile.TemporaryDirectory()
        self.tmp = Path(self._dir.name)

    def tearDown(self):
        self._dir.cleanup()

    def test_roundtrip_and_no_temp_left(self):
        target = self.tmp / "state.json"
        payload = json.dumps({"a": 1, "b": [2, 3]}, sort_keys=True) + "\n"
        write_atomic(target, payload)
        self.assertEqual(target.read_text(), payload)
        # No leftover temp from the unique-name mkstemp path.
        self.assertEqual(sorted(glob.glob(str(self.tmp / "*.tmp"))), [])

    def test_replaces_existing_completely(self):
        target = self.tmp / "state.json"
        write_atomic(target, json.dumps({"old": True}) + "\n")
        new_payload = json.dumps({"new": True, "n": 42}) + "\n"
        write_atomic(target, new_payload)
        self.assertEqual(json.loads(target.read_text()), {"new": True, "n": 42})

    def test_error_path_cleans_up_temp_and_keeps_old_file(self):
        target = self.tmp / "state.json"
        write_atomic(target, json.dumps({"keep": "me"}) + "\n")
        # os.replace blows up (target turned into a directory under it); the temp
        # must be unlinked and the prior file left intact.
        with patch.object(_mod.os, "replace", side_effect=OSError("boom")):
            with self.assertRaises(OSError):
                write_atomic(target, json.dumps({"lost": True}) + "\n")
        self.assertEqual(json.loads(target.read_text()), {"keep": "me"})
        self.assertEqual(sorted(glob.glob(str(self.tmp / "*.tmp"))), [])

    def test_concurrent_writers_never_expose_a_partial(self):
        target = self.tmp / "state.json"
        writer_py = self.tmp / "_writer.py"
        writer_py.write_text(_WRITER_SRC)

        n_writers = 4
        iters = 120
        blob_bytes = 120_000  # ~120 KB payload — big enough to interleave
        expected = {_expected_payload(w, blob_bytes) for w in range(n_writers)}

        # Pre-seed so the target always exists for the reader (atomic replace
        # never unlinks it, so it stays present for the whole run).
        target.write_text(_expected_payload(0, blob_bytes))

        procs = [
            subprocess.Popen(
                [sys.executable, str(writer_py), str(_SCRIPT), str(target),
                 str(w), str(iters), str(blob_bytes)])
            for w in range(n_writers)
        ]

        # Hammer the target with a RAW reader (no retry) while the writers run.
        # The unique-temp + atomic-replace guarantee means a raw read must ALWAYS
        # see one writer's complete payload — never a partial/spliced file.
        partial_reads = 0
        observed = set()
        reads = 0
        while any(p.poll() is None for p in procs):
            try:
                text = target.read_text()
                json.loads(text)  # raises on a truncated/spliced file
                observed.add(text)
                reads += 1
            except json.JSONDecodeError:
                partial_reads += 1
            except FileNotFoundError:
                partial_reads += 1

        for p in procs:
            self.assertEqual(p.wait(), 0, "writer subprocess crashed")

        self.assertEqual(partial_reads, 0,
                         "raw reader saw a torn/partial state.json")
        self.assertGreater(reads, 0, "reader never overlapped the writers")
        # Every complete snapshot the reader saw is exactly one writer's payload.
        self.assertTrue(observed.issubset(expected),
                        "reader saw a payload that was not any writer's complete output")
        # No temp files survive a clean run.
        self.assertEqual(sorted(glob.glob(str(self.tmp / "*.tmp"))), [])


class TestReadJsonRetry(unittest.TestCase):
    def setUp(self):
        self._dir = tempfile.TemporaryDirectory()
        self.tmp = Path(self._dir.name)

    def tearDown(self):
        self._dir.cleanup()

    def test_returns_parsed_on_valid_file(self):
        target = self.tmp / "state.json"
        data = {"ok": True, "xs": [1, 2, 3]}
        target.write_text(json.dumps(data))
        self.assertEqual(read_json_retry(target), data)

    def test_raises_last_error_when_never_valid(self):
        target = self.tmp / "state.json"
        target.write_text("{ not json")
        with self.assertRaises(json.JSONDecodeError):
            read_json_retry(target, attempts=3, backoff=0)

    def test_raises_filenotfound_when_absent(self):
        with self.assertRaises(FileNotFoundError):
            read_json_retry(self.tmp / "nope.json", attempts=2, backoff=0)

    def test_recovers_when_file_becomes_valid_mid_retry(self):
        target = self.tmp / "state.json"
        target.write_text("{ broken")  # invalid on the first attempt
        good = {"recovered": True}
        flipped = {"done": False}

        def fake_sleep(_):
            if not flipped["done"]:
                target.write_text(json.dumps(good))
                flipped["done"] = True

        with patch.object(_mod.time, "sleep", fake_sleep):
            self.assertEqual(read_json_retry(target, attempts=3, backoff=0.001), good)


if __name__ == "__main__":
    unittest.main()
