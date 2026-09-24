"""Ratchet: every bash test suite that stubs `gh` on PATH *and* invokes a
production subject reaching `gh` through Python `subprocess` must source
lib_hermetic.sh and call hermetic_poison_gh_env — the fail-closed backstop
for the native-Windows PATHEXT bypass documented in
scripts/fleet/CLAUDE.md's native-Windows PATHEXT rule.

The affected population is derived from the tree, not hand-listed: a
production script counts as a "python-gh subject" when it contains a
`subprocess.run([...` (or Popen/call/check_output) call whose first argument
is the literal `"gh"`, and a suite counts as a "PATH stub suite" when it
writes an executable file literally named `gh` under some directory it
prepends to PATH. A suite matching both is adoption-required.

KNOWN_SUBJECTS scopes the ratchet to the subjects this pass actually
fixed rather than the full tree-wide population: a wider census found
fleet-claim also reaches `gh` from embedded Python (contrary to its own
"pure bash" doc comment) across roughly fifty test suites — a separate,
larger migration tracked in its own follow-up issue rather than folded in
here unadopted-and-red. Every adopted suite keeps the poison-env backstop
even where its subject's python-reached code path isn't the one under
test, which is why the broader 53-suite set already carries
`hermetic_poison_gh_env` even though only the KNOWN_SUBJECTS below are
asserted here.
"""
import re
import unittest
from pathlib import Path

FLEET_DIR = Path(__file__).resolve().parent.parent
TESTS_DIR = FLEET_DIR / "tests"

GH_SUBPROCESS_RE = re.compile(r"subprocess\.(run|Popen|call|check_output)\(\s*\[?\s*[\"']gh[\"']")
# A fixed subject resolves gh once via shutil.which("gh") and reuses the
# result (see fleet-plan-lint's precedent) instead of passing the literal
# "gh" to each subprocess.run — match either shape.
GH_RESOLVED_RE = re.compile(r"""shutil\.which\(\s*["']gh["']\s*\)""")
GH_STUB_FILE_RE = re.compile(r"""["'][^"'\n]*[/\\]gh["']""")

# Scope of this pass's actual fix. See module docstring for why this isn't
# the full tree-wide subprocess-gh census.
KNOWN_SUBJECTS = [
    "fleet-queue-ingest",
    "fleet-decisions",
    "fleet-queue-backfill-model-labels",
]


def _stub_suites():
    for f in sorted(TESTS_DIR.glob("test_*.sh")):
        text = f.read_text(encoding="utf-8", errors="replace")
        if GH_STUB_FILE_RE.search(text):
            yield f, text


def _affected_suites():
    affected = {}
    for f, text in _stub_suites():
        hit = [s for s in KNOWN_SUBJECTS if s in text]
        if hit:
            affected[f] = (text, hit)
    return affected


class HermeticGhStubAdoption(unittest.TestCase):
    def test_known_subjects_are_present_in_the_tree(self):
        # A guard against the population silently going empty (e.g. a
        # rename) — see fleet-rules-sweep's zero-file-scope exit-2 rationale.
        for name in KNOWN_SUBJECTS:
            path = FLEET_DIR / name
            self.assertTrue(path.is_file(), f"subject {name} not found under {FLEET_DIR}")
            text = path.read_text(encoding="utf-8", errors="replace")
            reaches_gh = GH_SUBPROCESS_RE.search(text) or GH_RESOLVED_RE.search(text)
            self.assertTrue(
                reaches_gh,
                f"{name} no longer reaches gh via subprocess — "
                "narrow or drop it from KNOWN_SUBJECTS",
            )

    def test_affected_suites_adopt_lib_hermetic(self):
        affected = _affected_suites()
        self.assertGreater(
            len(affected), 0,
            "zero suites matched the PATH-stub + known-subject population — "
            "the detection heuristic likely broke, not a real all-clear",
        )
        missing = []
        for f, (text, hit_subjects) in sorted(affected.items()):
            sources_helper = "lib_hermetic.sh" in text
            calls_helper = "hermetic_poison_gh_env" in text
            if not (sources_helper and calls_helper):
                missing.append((f.name, hit_subjects, sources_helper, calls_helper))
        if missing:
            lines = [
                f"  {name}: subjects={subjects} sources_lib_hermetic={sources} "
                f"calls_poison_fn={calls}"
                for name, subjects, sources, calls in missing
            ]
            self.fail(
                "suites stub gh on PATH and invoke a known python-gh subject "
                "but do not adopt lib_hermetic.sh's poison-env backstop:\n"
                + "\n".join(lines)
            )


if __name__ == "__main__":
    unittest.main()
