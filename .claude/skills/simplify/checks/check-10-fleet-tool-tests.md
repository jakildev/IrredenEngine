# Check 10 — new fleet tool, function, or workflow logic with no test

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** the diff adds an
executable under `scripts/fleet/`, a new function to an already-tested
`scripts/**` module, or non-trivial logic in a workflow `run:` block.

Three arms, all report-only:

- **New executable under `scripts/fleet/`** (not under `tests/`): look for
  `scripts/fleet/tests/test_<name>.{sh,py}` (hyphens → underscores) or any
  test exercising the tool by name. No hit → "new fleet tool with no
  test_*; add one against a stubbed environment (`scripts/fleet/CLAUDE.md`
  §Authoring rules for the hermeticity bar)."
- **New function in an already-tested module** — a module-level `def` in a
  `scripts/*.py` with a `scripts/tests/test_*.py`, or a function in a
  `scripts/fleet/` executable with a `scripts/fleet/tests/test_*.sh`. The
  bar is a **direct** test case: a function reached only through an
  existing caller's test is untested on every path that caller doesn't
  take. Grep the sibling suite for the symbol's own name; no hit → "new
  `<name>` has no direct test — add one beside the precedent test for its
  neighbours."
- **Non-trivial bash inline in a changed `.github/workflows/*.yml` `run:`
  block** — defines a shell function, loops/branches over several
  commands, or exceeds ~15 lines. Flag "extract to a `scripts/fleet/*.sh`
  executable with a hermetic `tests/test_*.sh` and call it from the
  workflow" — inline `run:` logic has no CI signal and no local test.
