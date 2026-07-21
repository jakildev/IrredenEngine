## Plan: test: IRLightingSdfBlocker render-verify references stale on master (5/5 fail, likely post-#2293)

- **Issue:** #2344
- **Model:** sonnet — bounded bless task with a concrete visual checklist; the
  one judgment step has an explicit escalation tripwire (below), and the PR
  review re-judges the visual call from embedded before/after images.
- **Date:** 2026-07-20

### Scope

Re-bless the five `macos-debug/` reference PNGs for the `IRLightingSdfBlocker`
render-verify gate so it is green on current master. The issue's open question
("intentional change → bless, or regression → fix the bake") is **resolved at
plan time: bless.** Evidence below.

### Verified current state (plan-time, 2026-07-20)

- Refs last blessed by `5c242324` (#2313, Jul 8 19:34). The #2293
  coverage-splat merged `9568bb85` the same evening (22:56) — the issue's
  timeline is exact. #2356 (`b1aface6`) only **added** the separate
  `light-verify/` reference subtrees; it did not touch the five zoom shots.
  Only `macos-debug/` refs exist for this manifest — `linux-debug/` SKIPS by
  design, so **the acceptance run requires a macOS/Metal host**.
- **#2293 is a deliberate visual change**, not a regression: it closed #2270 by
  splatting cardinal single-canvas caster depth over an r=6 box, turning the
  moth-eaten cast shadow into solid coverage (before/after screenshots in PR
  #2293). The SdfBlocker scene is static cardinal-yaw-0 single-canvas — exactly
  the branch the splat engages on. The failure signature (scene-wide diffuse
  diff, uniform `max_delta=68` on all five shots) matches a shadow-coverage
  change, not a geometry/binding break.
- ~20 render commits have landed since the bless, several shadow-relevant
  (#2387 throw-limit unify, #2343 receive splat-aware bias) plus the #2431/
  #2432 edge-seam fixes. The delta at bless time is the **sum of intentional
  changes since Jul 8**, not #2293 alone — so the checklist below verifies the
  visual story against current master rather than attributing per-commit.
- **Measured this host (macOS, 2026-07-20, master `5c53d2e5`):**
  `fleet-build --target IRLightingSdfBlocker` dies in `_deps/fmt-build`
  `format.cc` consteval errors — #2449 gates even a previously-warm build dir
  once CMake re-runs. Since the task is macOS-only (above), it is
  **hard-blocked by #2449**; the issue body's `Blocked by:` field is updated
  accordingly as part of this plan so `fleet-claim` gates pickup until the fix
  lands.

### Approach

0. **Phase 0 (premise probe):** `fleet-build --target IRLightingSdfBlocker`.
   If it still dies in fmt, #2449 is not actually resolved on this host —
   comment the observation on the issue, release the claim, and stop.
1. **Positive control:** `python3 scripts/render-verify.py --target
   IRLightingSdfBlocker --demo lighting --no-build` — expect FAIL against the
   old refs with a scene-wide diffuse delta. (Numbers may differ from the
   issue's table — more render work has landed since; that is expected. A
   5/5 PASS here means someone already re-blessed — stop and close.)
2. **Visual delta check (the bless gate).** Inspect the captured shots and the
   harness's diff images against the old refs:
   - The wall's cast shadow band is **solid / wider** vs the old moth-eaten or
     thinner band (the #2293 signature).
   - The warm point-light gradient on the light side of the wall keeps its
     shape and anchor (no light moved, no falloff change — that path is
     #2293-untouched).
   - Silhouettes/geometry unchanged apart from pixel-scale edge-seam
     differences (#2431/#2432).
   - Nothing missing, black, inverted, or banded.
   **If any check fails, do NOT bless** — comment findings + captures on the
   issue and escalate per role step 8a (sonnet → opus): a wrong bless silently
   poisons this gate for every later render PR.
3. **Bless:** rerun with `--update-references --force`; then a plain rerun
   must report 5/5 PASS. Run the plain rerun twice — a shot that mismatches
   its own immediate re-capture means the scene's verified bit-determinism
   broke; that is a finding to escalate, not to bless around.
4. **PR:** first commit is `.fleet/plans/issue-2344.md` (this plan), then the
   five updated PNGs. Embed before/after of at least `zoom4_origin` (the
   harness diff artifacts are fine) so the reviewer re-judges the visual call.
   `Closes #2344`.

### Affected files

- `creations/demos/lighting/test/references/macos-debug/zoom1_origin.png`,
  `zoom2_origin.png`, `zoom4_origin.png`, `zoom4_offset_3_5.png`,
  `zoom8_origin.png` — re-blessed captures
- `.fleet/plans/issue-2344.md` — new (implementer's first commit)
- `creations/demos/lighting/test/references/manifest.json` — **unchanged**
  (do not loosen thresholds or drop shots to make old refs pass)

### Acceptance criteria

- Pre-bless run recorded in the PR: FAIL vs the #2313 refs (positive control —
  proves the harness sees the delta).
- Post-bless: `render-verify.py --target IRLightingSdfBlocker --demo lighting`
  reports 5/5 PASS on macos-debug at the PR's base, twice consecutively.
- The step-2 checklist is affirmed in the PR body with before/after images.

### Gotchas

- The fix is new references, **never** threshold/manifest edits.
- macOS-only task — do not route to a Linux host (Metal vs GL are
  pixel-different by design; no linux refs exist here to compare against).
- In-flight reconciliation: PR #2393 (sun-shadow softness, #2321) is parked
  WIP behind #2385 and will shift these shots again if it lands — normal
  churn; do not wait for it and do not co-bless with it. Siblings #2158/#1969
  are `canvas_stress` linux-debug refs — same family, zero file overlap.
- render-verify's default build dir is `<repo>/build` — run from the claimed
  worktree so the binary matches the checkout (stale-binary trap).
- The demo captures 6 `kShots[]`; the first 5 map positionally to the
  reference labels. `zoom16_origin` is intentionally excluded — don't add it.
