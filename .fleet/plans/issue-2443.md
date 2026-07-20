## Plan: fleet-plan-lint concept-based core-section matching (kill the destructive leading-token false positive)

- **Issue:** #2443
- **Model:** sonnet — single-file regex change in `scripts/fleet/fleet-plan-lint` plus two test fixtures; the approach below is concrete enough that implementation is bounded.
- **Date:** 2026-07-20

### Scope

Make `fleet-plan-lint`'s core-section check match the **concept** of each core heading
(Scope / Approach / Acceptance) rather than requiring the heading text to *begin* with the
literal token, so a sound plan that words its headings naturally no longer hard-fails. The
`>= 2`-missing hard-fail and every other check are preserved.

### Verified current state + confirmed repro

Read `scripts/fleet/fleet-plan-lint:104-112` on `origin/master` (5c53d2e5). The core-section
check anchors each token at the **start** of the heading:

```python
missing_core = [s for s in ("Scope", "Approach", "Acceptance")
                if not re.search(r"(?im)^\s*#{1,4}\s+" + s + r"\b", plan)]
...
if len(missing_core) >= 2: hard.append("missing core sections: ...")   # exit 1
elif missing_core:         warn.append(...)                            # exit 0
```

Confirmed the miss against the actual matcher (not the issue's guess) with the #2442-shaped
headings `### Files / modules`, `### Committed approach — …`, `### Acceptance tests (positive-fire)`:

- `Scope` → miss (heading leads with "Files"), `Approach` → miss (leads with "Committed"),
  `Acceptance` → match (leads with "Acceptance" by luck) ⇒ `missing_core = [Scope, Approach]`,
  len 2 ⇒ **hard fail (exit 1)** on a sound plan. Reproduced locally via the exact regex.

A hard fail here is **destructive**: per `role-opus-reviewer`, exit 1 routes straight to the
"Not sound" bounce, which **deletes the `## Plan` comment** before swapping back to
`fleet:needs-plan` — a false positive forces a full re-plan, not a warning.

The lint fetches via `gh issue view`, so its tests mock `gh` with a PATH-shim fake keyed by
issue number (`scripts/fleet/tests/test_fleet_plan_lint.sh:47-83`). Existing fixtures #100
(GOOD_PLAN) and #103 (skeletal, missing all core sections) already pin PASS / hard-fail.

### Approach

One approach, picked. Match the **concept**, not the leading token: per core section, accept
a small synonym set and allow the word **anywhere** in the heading (`.*\b(?:…)\b`). Keep the
`>= 2`-missing hard-fail and the `== 1` warn unchanged — only the per-section matcher changes.

1. In `scripts/fleet/fleet-plan-lint`, replace the `missing_core` comprehension (lines
   104-105) with a per-concept synonym-set search:

   ```python
   CORE = {
       "Scope":      ("scope", "files", "modules", "affected"),
       "Approach":   ("approach", "design", "implementation"),
       "Acceptance": ("acceptance", "verification", "tests", "done"),
   }
   missing_core = [
       name for name, syns in CORE.items()
       if not re.search(r"(?im)^\s*#{1,4}\s+.*\b(?:" + "|".join(syns) + r")\b", plan)
   ]
   ```

   Leave lines 106-112 (soft `Affected files`/`Gotchas` warns, the `>= 2` / `== 1`
   branch) and every other check untouched. `missing_core` stays a list, so the existing
   `>= 2` / `elif` logic and the `", ".join(missing_core)` message keep working.

2. Add two fixtures + assertions to `scripts/fleet/tests/test_fleet_plan_lint.sh`:
   - **#108 synonym-heading plan** (the false-positive regression): a plan whose headings are
     `### Files / modules`, `### Committed approach — …`, `### Acceptance tests (positive-fire)`,
     `### Gotchas` with a body. Assert **exit 0**. This is the red-against-master demonstration
     (AC 3): it exits 1 on master and 0 after the fix.
   - **#109 negative control**: a plan with a real `### Approach` section but **no** Scope-concept
     and **no** Acceptance-concept heading (e.g. `### Approach` + `### Notes` only) ⇒
     `missing_core = [Scope, Acceptance]`, len 2 ⇒ assert **exit 1**. This proves the broadened
     matcher did not become so permissive it passes a genuinely-broken plan.

### Affected files

- `scripts/fleet/fleet-plan-lint` — lines 104-105: leading-token matcher → concept synonym-set matcher.
- `scripts/fleet/tests/test_fleet_plan_lint.sh` — two new fixtures (#108, #109) + assertions.

### Acceptance criteria

1. The #2442-shaped synonym plan (`### Files / modules`, `### Committed approach — …`,
   `### Acceptance tests (positive-fire)`) lints **exit 0** (AC 1).
2. A plan missing ≥2 core concepts (no scope/files/modules/affected heading AND no
   acceptance/verification/tests/done heading) still hard-fails **exit 1** — negative control (AC 2).
3. `bash scripts/fleet/tests/test_fleet_plan_lint.sh` is green; existing fixtures #100 (pass) and
   #103 (hard fail) keep their verdicts (no regression).
4. **Demonstrate the red run**: run the new #108 assertion against `origin/master` (unfixed
   matcher) and confirm it fails there — proving the test exercises the new matcher, not a
   vacuous pass (AC 3).

### Gotchas

- **Exclude "plan" from the Approach synonym set.** Every plan comment's first heading is
  `## Plan: <title>`; a synonym set containing `plan` matches that title via
  `.*\bplan\b`, so **Approach would be satisfied for every comment** (vacuous — masks a plan
  that genuinely lacks an approach section). The issue's own concrete regex example
  (`\b(?:approach|design|implementation)\b`) already omits `plan` for this reason. Use only
  `approach | design | implementation`.
- **`### Affected files` will now also satisfy Scope** (via `files`/`affected`), because the
  #2442 case requires `files`/`modules` to count as scope. This is an accepted trade-off: the
  lint is conservative-by-design (it only hard-fails unambiguous structural gaps; the Opus
  design-soundness pass is the real gate), and a plan carrying an affected-files list does convey
  scope. Do **not** try to disambiguate Scope from Affected-files — it would re-introduce
  brittleness.
- **Keep `>= 2` hard-fail.** The threshold is retained per the issue's suggested shape; a plan
  missing exactly one concept stays a warn (exit 0), so the negative control (#109) must miss
  **two** concepts to hard-fail. Do not demote `missing core sections` to warn-only — AC 2
  requires genuinely-broken plans to still hard-fail.
- **Do not self-tag the issue `fleet:sonnet`.** The `**Model:** sonnet` here is the plan's
  implementation-class recommendation; the implementing worker picks it up on the normal class
  routing once queued.
