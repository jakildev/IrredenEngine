## Plan: pivot-verify — gate the SDF twin at its measured floor, composed with #2758's per-block bound

- **Issue:** #2851
- **Model:** opus
- **Date:** 2026-08-09 (plan rev 2, `fleet:plan-review` cleared 2026-08-21) / 2026-09-06 (implementation)

### Scope

`SDF_GATED = False` (#2648) removed the `focus-ctr` SDF twin's only gate instead of bounding it to the destination-grid floor its own evidence measures. The twin became the single pass in `scripts/pivot-verify.py` that could not fail at any deviation — a 200px silhouette drift on it exited 0 — so nothing machine-checked the SDF render path's pivot convention. This is the inverse of the defect #2648 fixed: #2645/#2646 established that a gate no *correct* implementation can pass is a defect; the remedy over-shot into a gate no *incorrect* implementation can fail. Riding along (issue §4): the ungated arm keyed on `focus == "-"`, a proxy that also matches any block classified in neither oracle set, so an unclassified 8th block would report `REPORT`/exit 0 where pre-#2648 it raised `KeyError`.

Committed approach: issue §7 option 1 (floor-aware threshold for the twin) plus the §4 loud-classification fix. §7 options 2–4 declined with reasons recorded in the issue's rev-2 `## Plan` comment.

### Verified current state (issue + rev-2 plan measured @ `5a1fdc9ec`; re-verified for implementation @ `f3e79a54`)

The blocking item from the rev-1 plan review was the in-flight reconciliation: **PR #2758 rewrites the same hunks and ships this plan's own mechanism** — a per-block, measurement-derived bound replacing `--max-deviation` for one pass. Landing order was decided as #2758 first (`Blocked by: #2641`), because #2758 was older, MERGEABLE, mid-author-loop and fixed a *live* gap, while #2851 is latent. **#2758 merged 2026-08-21**, so the implementation branches from master and composes.

Its landed form differs from the pending form the plan was written against: `CENTROID_BOUND_GAME_PX = {"center-axis": (1.5, 1.0)}`, evaluated as `scale * (px_per_zoom * zoom + floor_px)` where `scale` is the run's own `outputScaleFactor` read off the captured PNG (`_output_scale_factor`) — i.e. **affine, and stated in GAME-resolution px** rather than framebuffer px, because `outputScaleFactor` is a host display property and a framebuffer-px constant calibrated on one host silently mis-scales on the other.

### Approach

**Phase 0 — re-measure the floor premise.** Bail path: a twin reading > 3.0px on either axis refutes the premise (or is a live SDF regression) — stop, comment the measurement, design-block; never tune the bound to cover an unexplained reading.

**Phase 1 — the gate** (`scripts/pivot-verify.py`):

1. Replace `SDF_GATED = False` and its comment with `SDF_BOUND_GAME_PX = 2.5`, directly above `CENTROID_BOUND_GAME_PX`. The comment keeps the floor derivation and adds the bound's.
2. Route it ahead of the per-block table without touching #2758's expression: `if sdf: max_deviation = max(args.max_deviation, scale * SDF_BOUND_GAME_PX)` / `else: <#2758's landed expression, verbatim>`. The two branches are disjoint, so neither bound can displace the other.
3. Gate the twin: `centroid_gated = block in CENTROID_GATED_BLOCKS` — drop the `(SDF_GATED if sdf else True)` term.
4. Loud classification: in the block-validation loop, `SystemExit` naming both sets when a requested block is in neither `CENTROID_GATED_BLOCKS` nor `FOCUS_ASSERT_BLOCKS`, and assert `SDF_BLOCKS ⊆ CENTROID_GATED_BLOCKS` in the same up-front check (an SDF twin runs no focus oracle, so the centroid is its only gate). Delete the `elif focus == "-": verdict = "REPORT"` arm and drop `"REPORT"` from `passing`; the else-arm dict lookup then `KeyError`s on any impossible fall-through — the pre-#2648 loud behavior as a backstop.
5. Legend + docstring: state the twin's threshold; keep the FOCUS-OK sentence, which is true prose about the focus-assert blocks.

**Phase 2 — hermetic tests** (`scripts/tests/test_pivot_verify_gates.py`, new): t1 positive-fire (twin 200px → rc 1, row DRIFT); t2 floor at both host classes; t3 threshold wiring straddling the bound; t4 census — all 8 passes fail under the maximally-bad reading; t5 unclassified block → `SystemExit` before any capture; t5b `SDF_BLOCKS` containment; **t6 regression-lock on #2758's bound** — the semantic-revert merge the rev-1 review demonstrated now fails a named test.

**Phase 3 — sync the three doc surfaces:** `docs/design/camera-yaw-pivot.md` (deviation-4 pointer + the §"Not a deviation" conclusion), `engine/render/CLAUDE.md` (the pivot-verify paragraph's twin sentence), and the module docstring. Locate by sentence text, not line number.

### Implementation record (2026-09-06)

**Deviation from the plan's literal constant, and why.** The plan specified `SDF_MAX_DEVIATION = 3.5` — a flat *framebuffer*-px value (2.00px HiDPI floor + the 1.5px default budget) — written when #2758's unit convention was still pending. Phase 1.1 anticipated the affine landing and directed the twin be expressed as "its zoom-coefficient-zero case: same mechanism, `a = 0`, `b = floor + budget`". #2758 landed the game-px convention, so that instruction resolves to `SDF_BOUND_GAME_PX = 2.5` game px (1.0 floor + 1.5 budget), scaled at runtime — not a flat framebuffer constant. Shipping 3.5 framebuffer px would have given a 2× host 0.75 game px of margin over its floor and a 1× host 2.5 — a 3.3× sensitivity split across hosts, which is precisely the mis-scaling `CENTROID_BOUND_GAME_PX`'s own comment argues against. The game-px form gives both hosts 1.5 game px.

**Phase 0 measured** on Windows/OpenGL, `outputScaleFactor == 1` (1280×720 framebuffer, centroid_x 639.50): twin `dev_x` **1.00**, `dev_y` **1.00** framebuffer px = **1.00 game px**, against a 2.50px applied bound → PINNED. Voxel pass 0.47/1.00 against the 1.5 default → PINNED. This is the ~1.00px the design doc predicted for a 1× host and confirms the floor is one *game* pixel, not one framebuffer pixel — direct evidence for the unit change above. Bail path not triggered (1.00 ≤ 3.0).

**Positive control:** the new suite run against `origin/master`'s `pivot-verify.py` fails 8 assertions + 1 error, while t6/t6b (the #2758 locks) pass on both — the shape a regression suite for this defect must have.

### Affected files

- `scripts/pivot-verify.py` — bound constant + sdf-branch routing, twin gating, fail-fast classification, REPORT-arm deletion, legend, docstring
- `scripts/tests/test_pivot_verify_gates.py` — **new** hermetic suite (auto-discovered by `run_all.sh`, runs in CI via `render-harness-tests.yml`)
- `docs/design/camera-yaw-pivot.md` — deviation-4 pointer + floor-section conclusion
- `engine/render/CLAUDE.md` — the twin sentence in the rotation-pivot paragraph

### Acceptance criteria

1. **Twin can fail; the bound spares the floor** — t1 rc 1 with the twin row DRIFT; t2 rc 0 with it PINNED on both host classes; live phase-0 run exits 0 with the twin PINNED at its own threshold.
2. **Census 8/8** — t4 proves every pass can fail under the maximally-bad reading.
3. **Unclassified block fails loudly** — t5 `SystemExit` before any capture; t5b asserts `SDF_BLOCKS ⊆ CENTROID_GATED_BLOCKS`.
4. **Docs match enforcement** — `rg -n '"REPORT"' scripts/pivot-verify.py` → 0 hits; `rg -n 'SDF_GATED' scripts/ docs/design/camera-yaw-pivot.md engine/render/CLAUDE.md` → 0 hits; the docstring and both doc sentences describe the floor-aware gate. Deliberately untouched true prose (the focus-assert blocks' "reported but not gated", the FOCUS-OK legend fragment) stays — neither grep can fire on it.
5. **#2758's gate survives** — t6 green.

### Gotchas

- The floor is host-dependent in framebuffer px (2.00 at 2×, 1.00 at 1×) but constant in game px — which is why the bound is stated in game px and scaled by the run's measured `outputScaleFactor`.
- Do not touch `--max-deviation` or the voxel default: voxel `dev_y` runs to 1.36px against 1.5 — thin margins, explicitly out of scope.
- `jitter_probe` rc semantics stay: rc 2 → `SystemExit`; CRASH / NO-FRAMES rows keep failing unchanged.
- `--skip-sdf` still skips twin passes; the classification guard runs regardless of it.
- t3/t6 are the only hermetic covers for threshold routing — the comparison itself lives in `jitter_probe`, which the stubs replace.
