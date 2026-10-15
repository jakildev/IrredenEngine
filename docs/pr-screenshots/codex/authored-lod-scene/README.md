# Authored LOD scene: native witnesses and remaining visual observations

The opt-in `IRShapeDebug --lod-scene --pivot-origin` fixture passes six Debug
Metal captures, its attachment/residency assertions and exact return-to-start
comparison. This validates the fixture's stated invariants; **it does not certify
all visible geometry or shadows**. The subsequent complete ShapeDebug regression suite
passes [all 62 checks](full-regression.txt), including the six new LOD views. No Windows/OpenGL run or
performance qualification is claimed.

Captured October 14, 2026 UTC on macOS/M4 Max, with the fixture changes atop
`b794238c6`. Source, executable, staged input and driver hashes stayed constant
through the capture driver. See the [capture manifest](capture/manifest.json.gz)
and [native log](capture/positive/run.log.gz).

## What the scene exercises

Six flower roots reuse loaded prefab assets: three lazy, three resident, with
one resident root pinned to tier 0. Resident parts materialize on their first
in-band use, then retain identity while hidden. They are not allocated eagerly.
Loaded-asset reuse is not GPU geometry instancing. The two attached DENSE
variants retain disjoint 216-slot allocations: 432 allocated slots, one 216-slot
span selected at every shot. The carved fine sphere occupies only a subset of
that span; 216 is not an occupied-voxel count.

Terrain, moving flower/DENSE parents and changing camera yaw exercise ordinary
rendering and shadow paths. Motion advances only between automatic captures;
interactive mode permits manual zoom inspection but does not animate parents.
LOD follows zoom, not orthographic depth. The origin pivot is required for this
closed camera itinerary: the default camera-center surface pivot retains state
that can change framing after yaw. The registered reference run includes
`--pivot-origin`.

| Shot | Zoom / yaw | Global tier | Live / visible / hidden resident parts | Image |
|---|---|---:|---|---|
| Coarse | 1 / 0° | 4 | 8 / 7 / 1 | [capture](lod_scene_coarse.png) |
| Medium | 4 / 45° | 2 | 10 / 7 / 3 | [capture](lod_scene_medium_yaw45.png) |
| Fine | 16 / 90° | 0 | 15 / 12 / 3 | [capture](lod_scene_fine_yaw90.png) |
| Return | 4 / 180° | 2 | 12 / 7 / 5 | [capture](lod_scene_return_yaw180.png) |
| Coarse rotated | 1 / 270° | 4 | 12 / 7 / 5 | [capture](lod_scene_coarse_yaw270.png) |
| Restored | 1 / 0° | 4 | 12 / 7 / 5 | [capture](lod_scene_restored.png) |

The override root stays at tier 0 throughout. Tier families 4/2/0 are covered;
this itinerary does not test every threshold or intermediate tier 3/1. Parent
motion exercises translation; rotation and scale assertions execute at identity.
The final image is exactly equal to the initial image despite additional hidden
resident parts: [100% matching pixels, max delta 0](exact-replay.json). A
[one-channel mutation](one-channel-control.json) is rejected with exit 1 and
max delta 1. The displayed rounded match percentage alone cannot grade that
negative control.

## Assertions have deliberate failing controls

[Source-control manifest](source-controls/manifest.json.gz) records both expected
failures and restoration/build success. Setting resident flags false fails
`LOD row must use its authored residency`; omitting the DENSE parent relation
fails `LOD child must follow its moving parent translation`. Both occur at the
first shot. Source bytes and hashes were restored, and the positive origin-pivot
capture followed restoration. These mutations do not exercise every possible
retention-policy failure.

The earlier [closed-pose compiler control](closed-pose/compiler.log.gz) changes
only final zoom 1→2 and fails the camera-pose `static_assert`. Its source snapshot,
mutation and positive syntax command are archived separately; it was a
syntax-only rejection, not a native run. CLI conflicts with viewport portrait,
AO contact probe, cull eviction and yaw sweep fail before scene initialization.
Their logs record child exit 2 (`RESULT=CRASH`); the wrapper returns 1.

## Visual follow-ups remain

The [paired finite-face proof](finite-face-proof/README.md) resolves the apparent
blue DENSE sphere gaps at yaw180: its material coverage and face normals match
independent rays through all 136 occupied cells exactly. That does not qualify
AO or lighting intensity. Petal/stamen bands and comb patterns and floor-shadow
holes/ragged tails remain visible observations.
A separate clean six-shot `--no-shadows` capture retains the petal/stamen bands:
[yaw 180](no-sun-shadows/lod_scene_return_yaw180.png),
[fine yaw 90](no-sun-shadows/lod_scene_fine_yaw90.png). The
[control manifest](no-sun-shadows/manifest.json.gz) and
[log](no-sun-shadows/run.log.gz) preserve the command and cameras. AO and
ordinary face lighting remain enabled; this excludes sun visibility alone,
not every shading source. These observations establish neither the cause nor
a new regression. Further face/owner views and retained-field comparisons must
distinguish legitimate voxel steps from reconstruction defects.
Keep that investigation separate from the LOD lifecycle assertions. In
particular, do not blur silhouettes or broaden screenshot tolerances to hide it.

## Reproduce

Use an explicitly configured Debug tree; Release strips the assertions and logs:

```sh
IRREDEN_BUILD_DIR="$PWD/build" fleet-run --timeout 120 IRShapeDebug --lod-scene --pivot-origin --auto-screenshot 20 --window-mode offscreen
python3 docs/pr-screenshots/codex/authored-lod-scene/drivers/codex-lod-scene-captures.py --root "$PWD" --build-dir "$PWD/build" --output /tmp/lod-recapture
```

The capture driver records fresh paths/hashes, exact replay and CLI/comparator
negative controls. `drivers/codex-lod-source-controls.py` takes the same arguments
and deliberately changes source, builds with four CPU slots and restores/rebuilds
after each fault; run it only in an exclusive worktree/build slot. It performs no
benchmark. Evidence assembly itself performed no build or native run.

[Evidence inventory](evidence.json) records original and exported hashes.
PNG pixels are unchanged. Logs/manifests/patches are gzip-compressed where useful;
local account path prefixes become `${WORKTREE}`/`${ENGINE_ROOT}`. The exact
pre-normalization hash is retained for every artifact.
