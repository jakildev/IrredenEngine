## Plan: per-axis composite registration under non-integer effective camera offset (P2)

- **Issue:** #2546
- **Model:** opus
- **Date:** 2026-07-24
- **Epic:** #2544 — see `~/.fleet/plans/issue-2544.md` for full context
- **Blocked by:** #2545

### Verified current state (confirmed repro)

With the #2545 half-cell compensated at the harness level (focus = center + (0.5,0.5,0.5), 2026-07-22 session), the cardinal frames of the pivot-verify focus-ctr sweep pin to (0.00, 0.00) but EVERY non-cardinal frame (residual yaws of both signs across four brackets: π/6, π/4-boundary, 2π/3, 5π/4, 7π/4) sits at a constant ≈(−16, −9)px = (−1, −1.1) iso units at zoom 4. Constant across residual sign/magnitude ⇒ a path-registration offset, not an orbit. SDF probe (no per-axis route) is clean ⇒ voxel per-axis composite only. At zoom 8 the measured x offset was −11px (not 2× the zoom-4 value) ⇒ the divergence is not a pure world-space constant; it interacts with per-zoom rounding — measure both zooms.

### Affected files

- `engine/prefabs/irreden/render/systems/system_trixel_to_framebuffer.hpp` — `perAxisBase_` derivation from `getEffectiveCameraIso()` (the offset is non-integer whenever yaw ≠ 0 in CAMERA_CENTER mode).
- `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` — cardinal gather's `cameraTrixelOffset_` producer (the convention to match).
- `engine/render/src/shaders/v_peraxis_scatter.glsl` (+ `f_peraxis_scatter`, Metal twins) — where perAxisBase + roundHalfUp compose with the corner math.
- Possibly `ir_iso_common.{glsl,metal}` shared anchor helpers.

### Approach

Instrument first: log, per pivot-verify shot, the float effective offset and each stage's rounded anchor (`floor` vs `roundHalfUp` results). Find the stage pair whose disagreement equals the measured (−1, −1.1) iso px. Then unify: apply the pivot correction to the float offset BEFORE any per-stage rounding and use the SAME rounding convention on the per-axis route as the cardinal gather (the #1944 rule). Preserve the anti-vibration decomposition (integer game-px part + sub-pixel compensation) — the fix is convention alignment, not a new decomposition. Reconcile with the #2469 scatter-wobble accounting and the #2427 overflow neighborhood compare (same code region; do not regress their gates).

### Acceptance criteria

1. Full-sweep `pivot-verify.py --blocks focus-ctr,focus-off,center-column`: PINNED all 9 yaws, ≤1.5px, zoom 4 AND 8.
2. `--pan-sweep` / `--yaw-sweep` SMOOTH at zoom 2/4/8 (no #1944/#2427 regression).
3. Cardinal yaw-0 byte-identity (render-verify).
4. RESULT=CLEAN.

### Gotchas

- Verify against #2545's landed convention, not the session's harness-level compensation — re-run the baseline after rebasing on it.
- The per-axis path also feeds `RESOLVE_PER_AXIS_SCREEN_DEPTH` and the overflow lane; a base-anchor change shifts those consumers identically or shadows/overflow de-register (check the sun-shadow cast under yaw once, `--debug-overlay shadow`).

### Verification

Build + run the acceptance matrix on the authoring host; cross-host smoke labels as usual.

