# Plan: P4b-3 — detached re-voxelize world sun-shadow CAST (#1596)

- **Issue:** #1596 (epic #1553, P4b track #1576)   **Model:** opus   **Date:** 2026-06-09
- **Canonical design:** `docs/design/detached-revoxelize-world-light.md` — this file is the task
  pointer; the doc is the source of truth (Q2 mechanism revised 2026-06-09, see below).
- **Resume on:** PR #1626 (WIP branch `claude/1596-revoxelize-world-shadow-cast`).

## State (from PR #1626's NEEDS-DESIGN)
Q2 → (a) "second bake dispatch per opt-in detached canvas" was implemented (C++ + GLSL + Metal,
default path byte-identical) but does NOT render on Metal: the second in-tick bake dispatch reads
the detached canvas's R32I distance texture as EMPTY. Localized to the Metal image-atomic
scratch-buffer indirection (`metal_texture.cpp::bindImage` → scratch slot) not delivering a
non-main canvas's distance data to a second in-tick compute dispatch. The cited per-axis
precedent never did a foreign-texture bake read — it RESOLVES to a main-layout screen-space
depth texture first, then bakes that.

## Architect decision — Q2 mechanism revised: (a) → (a′) resolve-then-bake
Mirror the per-axis precedent FAITHFULLY: resolve opt-in detached canvases' distances (model
frame + `worldCellOffset`) into a **main-canvas-layout screen-space depth source**, then bake
that through the proven main-bake path. Never bind a foreign model-frame R32I canvas texture as
a bake source.

- **Reuse before adding:** P4b-1 already composites detached depth for the framebuffer
  depth-sort. FIRST check whether a main-layout texture containing detached caster depth already
  exists at BAKE time (or can be cheaply made available there). If yes, bake that — zero new
  resolve passes. If not, add ONE dedicated resolve: accumulate ALL opt-in detached casters into
  ONE shared screen-layout caster-depth texture (min-depth), then ONE extra bake. N casters must
  not mean N bakes.
- **Q3 (GL might work with the direct read) is moot:** even if GL tolerates the foreign read,
  backend-divergent cast paths are rejected. (a′) on both backends.
- **The Metal backend gap is real but separate:** filed as its own issue (foreign R32I texture →
  second in-tick compute dispatch scratch delivery), with the PR's diagnostics. Do not block the
  feature on a backend fix.
- Screen-space caveat is consistent with the engine's existing model: the sun bake already casts
  from camera-visible surfaces only (`docs/design/screen-space-sun-shadow-map.md`); the resolve
  inherits that, which is correct and not a regression.

## Definition of done — the artifact, not the mechanism
- A `worldPlaced_` detached re-voxelize solid casts a **visible sun shadow onto the #1587 SDF
  floor** in `canvas_stress --world-place-revox`, on macOS/Metal AND Linux/GL. Screenshots in the
  PR (before/after). This is the end-to-end proof the design doc names; code that builds clean
  but casts nothing is not done.
- Default (non-`worldPlaced_`) path stays byte-identical (the standing opt-in regression guard).
- P4b-2 receive unregressed (world-placed solids still darken in world shadow).
- GLSL + Metal in lockstep; render-verify references per the epic convention.

## Ownership
`[opus]`. Closes out the P4b stack (#1592 depth → #1617 receive → this). Resume from PR #1626;
keep the gather/dispatch plumbing that survives the mechanism change, replace the foreign-texture
bake read with the resolve.
