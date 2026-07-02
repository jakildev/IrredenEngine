## Plan: render: canvas_stress per-ENTITY priority-swap orbit demo (#2122 item 2)

- **Issue:** #2154
- **Model:** opus
- **Date:** 2026-07-01

### Scope
Add an opt-in `canvas_stress` scene (`--only orbitswap`) that spawns world-placed
detached units carrying the **per-ENTITY** foreground carrier
`C_EntityCanvas::depthPriority_` (#1958), so a priority unit wins the composite
depth contest (tier 1) against a world-tier unit at every camera yaw — a clean
front/back decision, no z-fight flicker. Gate it on the deterministic
`assertCompositeDepthTier` machinery from #2122 (`--depth-probe-assert <px>,tier=1`),
**not** committed image baselines. This is the remaining #2122 item-2 demo; item 1
(the per-trixel tier gate) already shipped, and the per-trixel interpenetration
demo already exists (`--only interpenetrate`, tier=2). This one isolates the
per-ENTITY carrier at **tier=1**.

### Verified current state
- **Group mechanism** (`creations/demos/canvas_stress/main.cpp`): `SpawnGroup` enum
  ends at `kGroupSmallZoom = 1u << 8` (main.cpp:211-221). `parseSpawnGroups`
  (386-425) maps names→bits; opt-in scenes gate on an explicit
  `xxxGroupRequested()` bit check (e.g. `interpenetrateGroupRequested()` :374-376),
  invoked in `initEntities()` (~1485). `groupEnabled()` is default-ON for
  base groups but the opt-in scenes never spawn unless their bit is named — so
  `--only orbitswap` isolates the pair and the default run stays byte-identical.
- **Per-ENTITY carrier**: `C_EntityCanvas::depthPriority_` (component_entity_canvas.hpp:41).
  `!= 0` ⇒ ENTITY_CANVAS_TO_FRAMEBUFFER pins the canvas into the reserved near band
  → `depthPriorityMode_ = 1`; `f_trixel_to_framebuffer` resolves
  `tier = max(perEntityTier /*=1*/, perTrixelTier /*=0*/) = 1`. Only meaningful when
  `!screenLocked_` (world-placed). Two-tier: **any non-zero value ⇒ the single
  foreground tier 1**. Confirmed live: `spawnDetachedVoxelObject` already sets
  `canvas.depthPriority_ = 1` on the canary cubes (main.cpp:493-543).
- **Deterministic gate**: `IRPrefab::DepthProbe::assertCompositeDepthTier(px, tier)`
  (depth_probe.hpp:222-239) emits `[depth-probe-assert] … tier=N expected=M result=PASS|FAIL`.
  Wired in main.cpp:1304-1326 behind `--depth-probe-assert X,Y,tier=N`
  (`applyDepthProbeAssert`, 950-969) — registered only when the flag is set, so a
  flagless run adds no system (byte-identical). `scripts/depth-tier-verify.py` drives
  `--only interpenetrate … --no-spin --no-auto-rotate --zoom 1
  --depth-probe-assert 639,362,tier=2` and exits non-zero on any FAIL. Its comment
  (lines 40-42) notes the probe pixel stays on the origin-centered far unit **across
  the whole rotating suite** (camera pivots about the world origin) — the reuse
  basis for an origin-centered orbitswap pair.

### Approach (single, committed)
Mirror `spawnPerTrixelInterpenetration` (main.cpp:866-901) but exercise the
per-ENTITY carrier at tier 1, origin-centered so the overlap pixel is stable and
deterministic.

1. **Enum + parser** (`main.cpp`): add `kGroupOrbitSwap = 1u << 9` to `SpawnGroup`;
   add `{"orbitswap", kGroupOrbitSwap}` to the `parseSpawnGroups` name table; add
   `bool orbitSwapGroupRequested()` (bit check, mirroring
   `interpenetrateGroupRequested()`); append `orbitswap` to the `--only` help string.
2. **Scene builder** `spawnPerEntityPrioritySwap()`: two world-placed detached
   units on **separate** `C_EntityCanvas`es (per-entity priority arbitrates across
   canvases at finalization; two sets on one canvas resolve at the canvas raster,
   upstream of the partition), both `screenLocked_ = false`, `RotationMode::
   DETACHED_REVOXELIZE`, stacked on the iso-depth axis at the same screen position:
   - **NEAR** unit — worldPos `(0,0,0)`, `depthPriority_ = 0` (world tier 0), color A.
   - **FAR** unit — worldPos `(6,6,6)` (behind on the (1,1,1) depth axis, so WITHOUT
     priority it is occluded), `depthPriority_ = 1` (entity-foreground → tier 1),
     color B. The carrier lifts it in front of the nearer world unit ⇒ the composite
     winner at the overlap decodes **tier 1**. This is the swap-not-flicker proof:
     the ordering is decided by tier, cleanly, not by a true-depth z-fight.
   - Do **NOT** call `changeVoxelPriorityAll` (that is the per-TRIXEL carrier → tier 2;
     this demo isolates the per-ENTITY carrier → tier 1).
   - Optional slow `C_AutoSpin` per unit for liveliness (`g_settings.noSpin_ ? 0 :
     kDetachedSpinBaseRadPerFrame`); frozen under `--no-spin` for the gate.
   The default camera yaw-ramp (`--auto-rotate`, already on) supplies the "orbit":
   the pair rotates about the origin focus while the far unit stays authoritatively
   in front across all yaws.
3. **Invoke** in `initEntities()` next to the interpenetrate block (~1485), gated on
   `orbitSwapGroupRequested()`.
4. **Reuse the gate** (`scripts/depth-tier-verify.py`): add an `--only <group>`
   argument (default `interpenetrate`, so existing callers are unaffected) threaded
   into the `run_cmd` `--only` token; the existing `--tier` / `--pixel` args already
   parameterize the assertion. Run orbitswap as
   `python3 scripts/depth-tier-verify.py --only orbitswap --tier 1 --pixel <overlap>`.
   Confirm `<overlap>` first with `--only orbitswap --no-spin --no-auto-rotate
   --depth-probe <px>` (logs the resolved tier); 639,362 is the starting candidate
   (same origin-centered placement as interpenetrate). Update the script docstring.

No new auto-screenshot shots and no manifest change — the base suite already frames
the origin-centered pair, exactly as interpenetrate relies on it. This keeps the
diff minimal and the default/manifest byte-identical.

### Affected files
- `creations/demos/canvas_stress/main.cpp` — new enum bit, `parseSpawnGroups`
  entry, `orbitSwapGroupRequested()`, `spawnPerEntityPrioritySwap()`, its gated
  invocation in `initEntities()`, `--only` help-string text.
- `scripts/depth-tier-verify.py` — `--only` arg (default `interpenetrate`) + docstring.
- *(optional)* `engine/prefabs/irreden/render/CLAUDE.md` — one-line demo pointer near
  the #1958 `depthPriority_` bullet (~line 596), analogous to the interpenetrate
  pointer at line 627.

### Acceptance criteria
- `--only orbitswap` spawns the per-entity-priority pair; the flagless default run
  and every existing manifest shot stay **byte-identical** (gate strictly on
  `orbitSwapGroupRequested()`; touch neither the base suite nor the manifest).
- `python3 scripts/depth-tier-verify.py --only orbitswap --tier 1 --pixel <overlap>`
  passes (composite winner at the swap overlap decodes tier 1); a dropped carrier
  would decode tier 0 and FAIL.
- `fleet-build --target IRCanvasStress` clean; `--only orbitswap --auto-screenshot 8`
  runs headless without crashing.

### Gotchas
- **World-placed only.** `depthPriority_` is a no-op when `screenLocked_` (composite
  gates priority on `!screenLocked_`). Do not pass `--screen-lock-detached`, and set
  `canvas.screenLocked_ = false` explicitly (not `g_settings.screenLockDetached_`).
- **Separate canvases per unit** — per-entity priority arbitrates ACROSS canvases at
  finalization; two voxel sets on one canvas resolve at the canvas `atomicMin`,
  upstream of the partition.
- **Set `depthPriority_` on the `C_EntityCanvas` before `createEntity(… canvas)`** —
  the composite reads the canvas the world entity carries.
- **tier 1, not 2.** Any non-zero `depthPriority_` selects the single entity-fg tier
  (1). Assert `tier=1`; per-trixel (`changeVoxelPriorityAll`) would give tier 2.
- **Far = priority.** Place the priority unit BEHIND the world unit so priority is
  load-bearing (without it the unit is occluded) — otherwise the gate proves nothing
  (a byte-identity-at-default trap: the OFF path is a no-op).
- **In-flight overlap with PR #2161 (OPEN).** #2161 edits the SAME file's
  `registerArgs()` (adds `--frozen-pose`, migrates `--debug-overlay` to `.enumValue`)
  and `initEntities()` (~1502-1522) — textually adjacent to the orbitswap
  invocation/help-string edits but **not** touching `--only`/`parseSpawnGroups`, so
  any conflict is mechanical, not semantic. If #2161 merges first, rebase and
  re-place the additions. Do NOT migrate `--only` to `.enumValue` here — that
  declarative migration is #2161's scope.
- **Sibling #2155** (#2122 item 3, perf fast-path in `f_trixel_to_framebuffer.glsl`)
  touches the shader, not the demo — no conflict.

