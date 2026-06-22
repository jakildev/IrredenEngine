# #1920 lattice-couple fix — #1922 jitter-harness measurements

Measured with `scripts/dev/shape-rotate-jitter-sweep <build> "<shapes>" 8 360`
(macOS / Metal, 2560×1440, zoom 8, 360 steps = 1°/step, 30 deg/s, SDF render
path, gate `MAX_JITTER=4.0`). `jitter_score` = mean interior |d²L| (lower =
smoother; the harness gate is 4.0).

## Clean base (pre-#1942, commit `a7793a58`) — the authoritative comparison

This base matches the harness doc's published master baseline
(`docs/design/jitter-validation-harness.md`), so it is the correct reference.

| shape         | master | + lattice-couple fix | Δ     | verdict (both) |
|---------------|--------|----------------------|-------|----------------|
| curved_panel  | 11.38  | **7.24**             | −36%  | FAIL → FAIL    |
| ellipsoid     | 7.54   | **5.58**             | −26%  | FAIL → FAIL    |
| sphere        | 1.20   | 1.39                 | +16%  | PASS → PASS    |

The fix **substantially reduces** the curved-SDF depth-band crawl (−36% /
−26%) and does **not** regress the clean control past the gate (sphere stays
PASS). But it is **insufficient to clear the 4.0 gate**: curved_panel needs
~−65% (11.38 → <4.0); lattice-coupling delivers ~−36%.

## Current master (c4cd39a0, includes #1942) — NOT a valid base

| shape         | master | + fix | note |
|---------------|--------|-------|------|
| curved_panel  | 12.53  | 8.43  | #1942 inflates every shape |
| ellipsoid     | 9.98   | 8.02  | |
| sphere        | 4.63   | 5.43  | clean sphere is 1.20 pre-#1942 — **#1942 alone pushes it to 4.63 (FAIL)** |

#1942 ("camera Z-yaw exact-screen-center default", `2aafa6b6`) is a rotation-
jitter regression on current master — independently reverted by #1944 / PR
#1953. It does not touch `c_shapes_to_trixel`; it perturbs the per-frame iso
projection so every shape's luminance shifts more frame-to-frame. **#1920
cannot be validated on master until #1953 lands** (master's own clean sphere
fails the gate today).

## Why lattice-coupling can't close the gate (mechanism)

The #1922 metric is the second temporal difference of luminance — it rewards a
*continuous* (sub-pixel) ramp and spikes on any frame-to-frame *toggle*.
`surfaceD` is an integer; under a continuous yaw sweep it steps between integer
depths, and each step toggles the recovered lighting position → a luminance
spike → d². Snapping `surfaceD` to the coarser 3-unit voxel lattice makes the
toggles **larger but less frequent** — it lowers the score (fewer toggles
dominate) but cannot remove it, because *any* integer-lattice depth quantization
toggles. Confirmation from the harness's own baseline: box/wedge are the
lattice-aligned shapes and they *also* FAIL — being lattice-aligned does not
confer temporal smoothness under a continuous sweep (the cardinal path is stable
because it is *static*, not because it is lattice-aligned).
