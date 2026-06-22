# #1944 follow-up — camera Z-yaw pivot hardening evidence

Render-verification evidence for commit `5322c261` (exact-center pivot +
detached composite on the effective offset). macOS / Metal, full engine build.
Images downscaled to 1280px wide. The `so3_*` files are the earlier #1944
*revert* evidence (commit `8ac0f784`), unchanged.

## shape_debug — the reported bug (camera panned to (16,16), then yawed)

| File | What it shows |
|---|---|
| `shape_debug_pan16_yaw0_BEFORE.png` / `..._yaw0_AFTER_identical.png` | yaw 0 — **byte-identical** (img_diff 0.0000%). The cardinal fast path is preserved. |
| `shape_debug_pan16_yaw180_BEFORE_swing.png` | **Before:** at yaw 180 the panned scene swings off-frame to the lower-left — the "vibration / swing on rotation" the issue reports. |
| `shape_debug_pan16_yaw180_AFTER_pinned.png` | **After:** content rotates in place about screen center. A landmark at screen-offset `(-dx,-dy)` from center maps to `(+dx,+dy)` — an exact point-reflection through center (correct 180° pivot). |
| `shape_debug_pan16_yaw180_DIFF.png` | img_diff before↔after = 15.9% (the swing being corrected). |

## canvas_stress — regression guard (un-panned, --no-spin --no-auto-rotate)

| File | What it shows |
|---|---|
| `canvas_stress_yaw45_BEFORE.png` / `..._AFTER.png` | The detached canary + GRID + floor stay coherent. yaw 0 is byte-identical (0.0000%); yaw 45 differs only **0.21%** (the small whole-composition pivot correction, detached + GRID moving **together**). |
| `canvas_stress_yaw45_DIFF.png` | img_diff before↔after = 0.21% — thin boundary pixels only. Contrast the **1.3–3.4%** detached *drift* that #1942 alone caused: the detached-follows-effective change keeps the #1944 fix intact. |

## Not addressed here

The same `shape_debug` scene shows "flat-top, no-depth" shapes near the floor
(e.g. the orange/green shapes sitting on the platform). That is **Bug A** from
the #1884 depth-unification investigation — the iso-depth (`x+y+z`) convention
ranks a grounded solid's lower faces behind the SDF floor, which clips them
(spreads with zoom). It is the design-escalated **#1958** sub-epic (unified
quadrant-stable depth encoding + priority bands, blocked by #1957), independent
of this pivot change.
