# Check 10 — new `scripts/fleet/` executable with no test

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** the diff adds an executable under `scripts/fleet/` or non-trivial logic in a workflow `run:` block.

The review checklist's "new feature with no new test" rule applies to fleet
tooling, where it's mechanically checkable at authoring time (PR #2232's
`fleet-gh-token` burned a review round-trip on it). For each newly-added
executable file under `scripts/fleet/` (not under `tests/`), look for a
matching `scripts/fleet/tests/test_<name>.{sh,py}` (hyphens → underscores)
or any test file exercising the tool by name. No hit → flag: "new fleet
tool with no test_*; add one against a stubbed environment (see
`scripts/fleet/CLAUDE.md` §Authoring rules for the hermeticity bar)."
Report, don't auto-fix.

The same rule fires on **non-trivial bash embedded inline in a changed
`.github/workflows/*.yml` `run:` block** — logic that defines a shell
function, loops/branches over multiple commands, or exceeds ~15 lines.
Flag: "extract to a `scripts/fleet/*.sh` executable with a hermetic
`tests/test_*.sh` and call it from the workflow" — inline `run:` logic
ships with zero CI signal and no local-sandbox test (#2290's `net_patch_id`
classify bug). Report, don't auto-fix.
