# Plan: retire the manual scatter dilation margin/miter/yield tower + full validation (C3)

- **Issue:** #1939
- **Model:** opus
- **Date:** 2026-06-21
- **Epic:** #1933 — see `~/.fleet/plans/issue-1933.md` for full context (incl. the cross-system audit of every retired symbol)
- **Blocked by:** #1938, #1922

## Scope

With analytic coverage authoritative on both backends (#1937 GL, #1938 Metal),
retire the now-redundant conservative-dilation margin/miter/yield heuristics
from the per-axis scatter pass, then run the full temporal-stability + solidity
validation and update the docs.

## Verified current state

The heuristics to retire (all shader-side `const`/`constant`; **no CPU-side
uniform plumbing** — `grep` over `engine/**/*.{hpp,cpp}` finds none):

| Symbol | Line (glsl / metal) | Role (post-#1937/#1938: redundant) |
|---|---|---|
| `scatterConservativeDilation()` | 768 / 678 | per-axis `0.5·|n|` + miter growth |
| `kScatterDilateMarginPx` | 691 / 636 | fixed camera-path margin floor |
| `kScatterDetachedPitchFraction` | 738 / 660 | detached pitch-proportional floor |
| `kScatterMiterLimit` | 723 / 654 | acute-corner blow-out cap |
| `kScatterMarginDepthBiasKey` | 701 / 642 | margin tie-break depth bias |
| `kScatterMarginYieldGradScale` | 717 / 650 | margin yield gradient |

Consumers: `v_peraxis_scatter.glsl:211,217-218`, `f_peraxis_scatter.glsl`,
`metal/peraxis_scatter.metal:194,198-200`, and the definitions in
`ir_iso_common.{glsl,metal}`. No other caller.

## Affected files

- `engine/render/src/shaders/ir_iso_common.glsl` + `…/metal/ir_iso_common.metal`
  — remove/reduce the six symbols above (in lockstep across both backends).
- `engine/render/src/shaders/v_peraxis_scatter.glsl` +
  `…/metal/peraxis_scatter.metal` — drop the per-axis miter call; keep only the
  minimal fixed visit-bound from #1937.
- `engine/render/src/shaders/f_peraxis_scatter.glsl` +
  `…/metal/peraxis_scatter.metal` — remove the margin-vs-interior classification
  + depth-yield bias (every surviving fragment is now interior).
- `engine/render/CLAUDE.md` — rewrite the convex-corner drift note (#1917): the
  drift is fixed; describe the analytic coverage model.

## Approach

1. Confirm (via #1937/#1938 results) that the margin depth-bias and yield-gradient
   are truly dead — no same-plane tie or ridge-bleed reappears with the margin
   reduced to the visit-bound. If one does, the analytic coverage isn't fully
   authoritative → fix coverage, do NOT re-add the bias.
2. Remove the symbols from BOTH backends together; reduce
   `scatterConservativeDilation` to the minimal visit-bound (or fold it inline).
3. `grep` `engine/` clean for every retired `kScatter*` name.
4. Update `engine/render/CLAUDE.md`.

## Acceptance criteria

- **Temporal stability** under the #1922 jitter harness on both backends (the
  core bar; needs #1922 on master).
- Coarse-cube near-cardinal: crisp corners + solid faces with the margin tower
  removed (regression-free vs #1938).
- `perf-grid-rotate-sweep` (epic #1881 solidity gate) passes; cardinal
  byte-identical; both backends at parity.
- No dangling references to retired symbols (`grep` clean).

## Gotchas

- Don't half-retire — all six symbols are defined in **both** backends (see the
  glsl/metal columns above; the metal `constant` mirrors live at
  `ir_iso_common.metal:636,642,650,654,660,678`). Remove all six from both
  `ir_iso_common.glsl` and `ir_iso_common.metal` in lockstep; leaving any as a
  dead `constant` in the Metal file trips the "`grep` clean" acceptance criterion.
- The depth-bias (#1457) + yield-gradient (#1883) exist because the margin
  over-fills; removing them is correct ONLY if analytic coverage left no
  over-fill. Validate before removing.
- Keep scatter depth co-sorted with the SDF #1370 metric.
- `ir_iso_common` shared with #1920 — serialize per #1881's one-at-a-time rule.

## Verification

- Both backends: `fleet-build`, run the affected demos with `--auto-screenshot`.
- The #1922 jitter harness (once on master) — temporal-stability gate.
- `scripts/dev/perf-grid-rotate-sweep` — solidity/silhouette.
- `render-verify` — cardinal byte-identity; `backend-parity` — GL↔Metal.

## Amendments

### A1 — 2026-07-13 — trigger: PR #2013 merged (#1937 C1)
- **Decision:** The per-backend attribution in this plan is **inverted**. C1
  (#1937) shipped **Metal** (PR #2013), and C2 (#1938) is the **GL** parity
  port — read "#1937 Metal, #1938 GL" wherever this plan says the reverse. The
  retire-the-tower work is backend-symmetric and **unchanged**: this child
  still removes the six `kScatter*` margin/miter/yield symbols from **both**
  `ir_iso_common.glsl` and `ir_iso_common.metal` in lockstep, once #1938 lands
  GL analytic coverage (both backends then authoritative).
- **Supersedes:** only the "#1937 GL, #1938 Metal" attribution in **Scope**. The
  retire targets, the symbol table, `Blocked by: #1938, #1922`, and the
  acceptance criteria are all unaffected.
- **Acceptance criteria:** unchanged.
- **By:** epic-steward — source: PR #2013 (merged Metal shaders); updated child
  issue titles #1937/#1938.
