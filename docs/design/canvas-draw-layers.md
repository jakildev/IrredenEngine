# Canvas draws at a composite depth

A creation that wants text, titles or bars *in* its picture, behind the
voxels, among them or in front of them, and torn by the glitch pass with
them, cannot get there from the GUI canvas: that canvas is drawn on screen
after the framebuffer, clean of everything. `IRPrefab::CanvasDraw` is the
missing piece: immediate-mode text, voxel text, rectangles and borders
queued onto **any named canvas at a chosen depth**, flushed once per frame
by `CANVAS_DRAW` through the text compute path.

Code: [`canvas_draw.hpp`](../../engine/prefabs/irreden/render/canvas_draw.hpp)
(the queue API, `createLayer`, the depth constants),
[`system_canvas_draw.hpp`](../../engine/prefabs/irreden/render/systems/system_canvas_draw.hpp)
(the flush), [`lua_canvas_draw_bindings.hpp`](../../engine/script/include/irreden/script/lua_canvas_draw_bindings.hpp)
(`IRCanvas`), the `C_CanvasDrawQueue` singleton and the `C_CanvasDrawLayer`
tag.

## Using it

```cpp
renderPipeline.push_back(IRSystem::createSystem<IRSystem::CANVAS_DRAW>());  // after TEXT_TO_TRIXEL, before TRIXEL_GLITCH
```

```lua
IRCanvas.createLayer("overlay")                      -- once, at setup
-- every frame, anywhere before CANVAS_DRAW runs:
IRCanvas.voxelText("overlay", "SYNTH ONE", 14, 28, { color = { 232, 163, 61 }, voxelSize = 2, distance = IRCanvas.Depth.BEHIND })
IRCanvas.text("overlay", "SAMPLE RATE", 14, 70, { color = { 220, 224, 232 }, distance = IRCanvas.Depth.BEHIND })
IRCanvas.fillRect("overlay", 110, 72, 3, 6, { 232, 163, 61 }, IRCanvas.Depth.BEHIND)
IRCanvas.drawBorder("gui", 100, 50, 60, 15, { 232, 163, 61 }, 1)   -- the clean GUI canvas, front depth
-- a meter of 16 cubes, 9 lit, climbing to the right along the world X axis:
IRCanvas.voxelRun("overlay", 110, 72, { count = 16, lit = 9, voxelSize = 2, axis = IRCanvas.Axis.X,
                                        color = { 232, 163, 61 }, unlitColor = { 40, 44, 56 } })
```

A voxel run is the voxel letters' projection applied to a line of cubes:
`IRCanvas.Axis.X` climbs to the right (/), `Y` drops to the right (\),
`SCREEN` runs level along the letter plane. `measureVoxelRun` gives the
box, `voxelRunCell(i, ...)` where cube `i` sits in it (for a marker at the
lit end). Cubes paint farthest first, so a later cube hides the one behind.

`LuaScript::bindLuaCanvasDraw()` installs `IRCanvas` (opt-in, like
`IRFog`). A canvas is named as `IRRender.getCanvas` knows it; `createLayer`
registers its canvas under the given name. Every option key is checked
before anything is queued, so a typo raises at the call.

## Decisions

1. **Depth is the layer.** The framebuffer composite depth-tests every
   canvas texel (`TRIXEL_TO_FRAMEBUFFER`, GL_LESS on the distance texture),
   so where a draw lands is the distance it writes, not which canvas holds
   it. The constants name the useful bands: `kDepthBehind` (one step in
   front of a cleared canvas, behind every voxel), `kDepthWorldNear ..
   kDepthWorldFar` (the world's own band, where geometry occludes the
   draw), `kDepthFront` (the far edge of the reserved foreground band:
   in front of all world content, behind foreground-priority entity
   canvases). A draw on the GUI canvas composites on screen after the
   glitch pass, which is the one way to stay clean.
2. **A layer canvas is the main canvas's twin.** `createLayer` makes a
   trixel canvas the main canvas's size, parented to the main framebuffer,
   with the screen-locked render behaviour and the GUI canvas's
   `LOCAL_TRIANGLES` display, so its texel space is the GUI canvas's at gui
   scale 1 and its text looks like GUI text. It carries no producer:
   `CANVAS_DRAW` clears it every frame.
3. **One compute path, no CPU uploads.** Text and voxel text expand through
   the same `GuiText` layout as `TEXT_TO_TRIXEL`; a rectangle is tiled by
   the solid glyph cell voxel text already paints with (at most 6 x 11
   texels per command). Every batch is one `dispatchGlyphCommands` per
   canvas with that canvas's colour and distance images bound. `CANVAS_DRAW`
   borrows `TEXT_TO_TRIXEL`'s program and command buffer; without them the
   queue is dropped, as `dispatchGuiText` drops widget text.
4. **Queued, not immediate.** `IRGui.fillRect` writes the GUI canvas the
   moment it is called, so it must run after the canvas's clear. A canvas
   draw is queued into the `C_CanvasDrawQueue` singleton (one batch per
   target canvas, capacity kept across frames) and rasterized when
   `CANVAS_DRAW` runs, so a UPDATE or RENDER system may issue it anywhere
   before that stage. A compute dispatch orders nothing, so the flush
   rasterizes a batch in painter's-order groups: each queued draw records
   the texel box it may write (`Batch::Primitive`), and a draw that meets an
   earlier one in the group starts the next dispatch (`overlapGroupEnd`);
   disjoint draws share one. A box is fill, border, then text: three
   dispatches, each fenced, each landing on the last.
5. **Placement.** `CANVAS_DRAW` runs after `TEXT_TO_TRIXEL` (its resources,
   and the GUI canvas's clear), after the widget renderers and the help
   overlay (a GUI-canvas draw paints over them), after
   `VOXEL_TO_TRIXEL_STAGE_1` (the background canvas's clear), and before
   `TRIXEL_GLITCH` / `TRIXEL_TO_FRAMEBUFFER`.

## Headless coverage

`test/script/lua_canvas_draw_bindings_test.cpp` drives `IRCanvas` with an
injected canvas resolver and layer factory (bare `C_SizeTriangles`
entities), asserting what each call queues: the per-glyph commands and
their depth, the solid-cell tiling and clipping of rectangles, the four
strips of a border, a voxel text's face runs, layer creation, and every
rejection.
