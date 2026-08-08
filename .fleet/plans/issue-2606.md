## Plan: jitter_probe — per-axis excursion assertion for the rotation gate

- **Issue:** #2606
- **Model:** opus
- **Date:** 2026-08-07

### Scope

Close the scorer-model gap #2606 measures: `jitter_probe`'s default verdict cannot express "x stays pinned while y may translate", so a smooth systematic centroid migration (the `IR_PERAXIS_OVERFLOW_DISABLE=1` defect signature, +11.13px monotone on 2026-07-28) scores clean on the axis it corrupts. Add per-axis excursion assertions (`--max-excursion-x` / `--max-excursion-y`), make the excursion check the primary rotation-gate assertion in the docs, and lock the semantics with a hermetic synthetic test plus live positive-fire runs.

Out of scope: the `--max-residual` bar, the #2469 accepted-residual table, and master's residual drift at zoom 4/8 — that is **#2907**'s (see Reconciliation).

### Verified current state (2026-08-07, origin/master)

- `tools/jitter_probe/main.cpp` (391 lines, read in full): default verdict = `reversals==0 && maxAbsResidual<=--max-residual` on **both** axes (`main.cpp:368-370`); `--stationary` = deviation-from-frame-0 ≤ `--max-deviation` on **both** axes (`main.cpp:337`). No per-axis-independent assertion exists anywhere in the scorer, and excursion (max−min of a centroid column) is computed nowhere — the negative claim is exhaustive over the tool's single source file.
- The defect fixture exists on master: `IR_PERAXIS_OVERFLOW_DISABLE` presence-check at `engine/prefabs/irreden/render/systems/system_trixel_to_framebuffer.hpp:504`.
- **The issue's AC2 phrase "using the same captured sequences above" is no longer satisfiable.** Those sequences were build-dir transients (`save_files/screenshots/`, wiped per the #2814 recipe) and are gone; all populations must be re-captured. Master has also drifted since 2026-07-28: **#2907** measures the same canonical probe's x residual at 0.85/1.78/2.70px (z2/z4/z8) vs the 1.50px bar — red at z4/z8 on the *residual* criterion. AC2's healthy-master leg is therefore re-anchored to fresh same-session captures (Phase 0), and the composite exit code at z4/z8 is decoupled from this task (Acceptance 3).
- Output consumers audited (full `git grep -l jitter_probe` sweep, every hit classified): the ONLY stdout parser is `scripts/pivot-verify.py:155-157`, which regex-parses the `--stationary` summary's `x: max_deviation=…px` lines. Nothing parses the default-mode summary. All other hits are docs/comments/CMake registration (`camera_pan_pivot_test.cpp:18` and `ir_args.cpp:11` are comments).

### Approach

**Phase 0 — premise probe: excursion still separates the populations on today's master.** The bar the docs will publish must be derived from *current* populations, not the dead 2026-07-28 captures (#2907 proves the tree moved). On one host/backend in one session, capture with the canonical recipe (`engine/render/CLAUDE.md` §"Verifying temporal stability": wipe → sweep → 6-digit glob → `--expect-frames`), archiving each population to its own directory immediately after capture:

- H2/H4/H8 — healthy voxel cylinder `--yaw-sweep`, zoom 2/4/8.
- S4/S8 — SDF cylinder control (omit `--spin-shape-voxel`), zoom 4/8.
- D2/D4/D8 — `IR_PERAXIS_OVERFLOW_DISABLE=1` voxel cylinder `--yaw-sweep`, zoom 2/4/8.

Hand-measure per-axis excursion (max−min of the `--verbose` centroid columns) for every population. Derive the per-zoom x-bar by the committed rule: **bar(z) = the smallest half-integer ≥ 2.5 × the max healthy x excursion at that zoom (voxel and SDF both count as healthy), and it must be ≤ 0.5 × the defect x excursion at that zoom.** (2026-07-28 reference: healthy 1.26–2.83px, SDF 2.00px, defect 11.13px @ z4 → z4 bar would land at 5.0; expect the same order today.) The 2.5× margin absorbs run-to-run noise on a noise-dominated statistic; the 0.5× ceiling keeps ≥2× headroom to the defect.

*Bail path:* if any zoom has no value satisfying both bounds, the premise (excursion separates healthy from defect on current master) is refuted — stop, post the full measurement table on #2606 and cross-reference it on #2907, and flag #2606 for re-plan noting the likely resolution is `Blocked by:` #2907. Do not build the dependent phases.

**Phase 1 — the flags** (`tools/jitter_probe/main.cpp`):
- Compute per-axis excursion = max−min of the centroid column over the **same valid-frame mask the fit uses**, unconditionally; append `excursion=%.2fpx` to the two default-mode axis summary lines, and the provided bars to the thresholds line.
- Add `--max-excursion-x <px>` / `--max-excursion-y <px>`, each independently optional, gated on `parser.wasProvided(...)` (not a sentinel default). When provided, AND `excursion ≤ bar` for that axis into the default-mode SMOOTH verdict.
- Combining either flag with `--stationary` is an argument error (exit 2, message) — prevents a silently-ignored assertion, and keeps the `--stationary` output path byte-identical for `pivot-verify.py`.

**Phase 2 — hermetic semantics test** (`test/tools/jitter_probe_excursion_test.sh`, new; mirrors `ir_run_exit_code_test.sh`'s staged-fixture form): binary from `$JITTER_PROBE_BIN` else the default build path, SKIP + exit 0 with a message when absent (nothing in CI runs `test/tools/` — this is a hand/review artifact). Fixtures: an inline `python3` heredoc writes two 8-frame sequences of 48×48 binary PPMs (stb_image reads PPM — no PNG encoder needed): (A) a 12×12 white square stepping +2px/frame in x, y fixed; (B) x fixed, +2px/frame in y. Five assertions, one probe invocation each:
1. A, default flags + `--reversal-eps 0.8` → **exit 0** — pins the blind spot (the OFF arm of the discriminating pair).
2. A + `--max-excursion-x 5` → **exit 1** — 14px migration; the gate's positive fire.
3. B + `--max-excursion-x 5` → **exit 0** — the issue's exact contract: x pinned while y translates 14px.
4. B + `--max-excursion-y 5` → **exit 1** — axis independence, and proves case 3's pass is non-vacuous (same frames, mirrored assertion, opposite verdict).
5. `--stationary --max-excursion-x 5` → **exit 2**.

**Phase 3 — live acceptance runs** (re-score the archived Phase-0 populations with the built tool):
- Cross-check the instrument: tool-printed x excursion on H4 must equal the Phase-0 hand-derived max−min to ±0.01px.
- D4 and D8 under the canonical flags + `--max-excursion-x bar(z)` → exit 1; then an isolation run adding `--max-residual 99` → still exit 1, so the failure is attributable to the excursion criterion by construction (not caught "by accident" via another axis — the exact failure mode the issue documents).
- H2/H4/H8 + S4/S8: printed x excursion ≤ bar(z) for every population. Composite exit codes recorded as-is: z2 expected 0; z4/z8 follow #2907's state (red today via the residual axis) — record the residuals and cross-comment them on #2907. If #2907 has landed by implementation time, expect composite 0 at all three zooms and say so.

**Phase 4 — docs** (AC3):
- `tools/jitter_probe/README.md`: usage block gains the flags; rewrite §"Accepted floors, and the model's blind spot (#2469)" — the false-negative half is closed by the excursion assertion (keep the reversal-eps history); drop the "until #2606… read excursion by hand" paragraph.
- `engine/render/CLAUDE.md` §"Verifying temporal stability": the canonical rotation-gate recipe gains `--max-excursion-x <bar(zoom)>` with the measured per-zoom bar table (populations, values, date, host); flip the "does not fire on the `IR_PERAXIS_OVERFLOW_DISABLE=1` runtime control" bullet to its measured opposite (fires via excursion: value vs bar); replace the closing "principled fix is #2606" paragraph. Do **not** touch the #2469 accepted-residual table or `--max-residual` — #2907 owns those.

### Affected files

- `tools/jitter_probe/main.cpp` — excursion computation + print, two flags, verdict, stationary-combination guard
- `tools/jitter_probe/README.md` — flags + blind-spot section rewrite
- `engine/render/CLAUDE.md` — recipe + bar table + limitation-paragraph replacement
- `test/tools/jitter_probe_excursion_test.sh` — new hermetic semantics test
- `.fleet/plans/issue-2606.md` — this plan, first commit of the PR (#1932)

### Acceptance criteria

1. **AC1 as filed:** the per-axis flags exist and are independently usable (test cases 3/4 prove independence on identical frames).
2. **AC2 re-anchored** (filed wording depends on dead captures + a since-drifted master, per Verified current state): D4 exits 1 with the excursion criterion attributably firing (isolation run), and every freshly captured healthy population (H2/H4/H8, S4/S8) passes the excursion criterion (printed x excursion ≤ bar(z)). z2 composite exit 0. z4/z8 composite recorded either way with residuals cross-commented on #2907.
3. **AC3 as filed:** both docs updated as Phase 4, carrying the measured table.
4. The synthetic test ships and passes 5/5 against the freshly built binary (run log in the PR body).
5. `pivot-verify.py`'s parse surface is untouched by construction (`--stationary` output byte-identical; combination guard makes the new flags unreachable in that mode) — stated in the PR body.

### Gotchas

- `IR_PERAXIS_OVERFLOW_DISABLE` is a `getenv != nullptr` presence check — `VAR=` (empty) counts as SET. Fully `unset` it for healthy arms and verify with `env`.
- Wipe the screenshots dir before **every** capture (exact `find … -delete` form in the CLAUDE.md block); archive each population before the next capture; `--expect-frames` must match the actual post-wipe file count. Note a live doc inconsistency: #2907/#2605 report 24-frame sweeps from `--auto-screenshot 6`, while the CLAUDE.md recipe comment says the guard should equal the auto-screenshot count and passes 6 — trust the actual file count (the guard exits 2 on a mismatch); if it is 24, fix the stale comment line in the Phase-4 docs pass.
- `fleet-run --timeout 0` goes **before** the target token, and redirect `fleet-build` output to a file + check `$?` — piping through `grep`/`head` masks a failed build as green and a stale binary then "passes".
- Capture all populations on ONE host/backend in one session; the published bar table names its host. The 2026-07-28 numbers are context, never controls.
- Keep the #2907 seam clean: if #2907's PR is in flight against the same CLAUDE.md section, whoever rebases second reconciles only the shared §temporal-stability paragraphs — the two tasks change disjoint criteria.

### Reconciliation (siblings / in-flight)

- **#2907** (open, needs-plan): the *residual*-axis drift on the same probe — the opposite failure class, per its own "Not #2606" ruling. This plan deliberately leaves the residual criterion untouched and hands #2907 free evidence (fresh residual readings cross-commented).
- **PR #2850 / #2479** (design-blocked): same render domain; measured digit-identical to master on this probe (#2907's ruled-out section) — no interaction.
- **pivot-verify / #2553**: `--stationary` consumer, parse surface audited and preserved.
- **Merged #2605** (reversal-eps recipe + limitation docs) and **#2814** (`--expect-frames`): extended, not contradicted — the limitation text #2605 added is exactly what Phase 4 replaces.

