# High-density cardinal canvas coverage

Parent: `89c14c7bd`; macOS Metal Debug, 1280×720 game framebuffer, 2560×1440 capture.
`manifest.json` records commands, binary hashes, capture hashes and final engine
source hashes. Captures come from clean native shutdowns. The parent was rebuilt
from its engine files in the dedicated worktree, then the candidate was restored
and rebuilt. No shader algorithm, blur, face expansion or reference tolerance changes.

The 64³ single-voxel frozen wave uses zoom 4, FULL base 4 (effective density 16).
The parent draws only a central 642×360 display-pixel rectangle because its
642×722 backing clips density-scaled writes. The corrected 2570×2890 backing
preserves texel screen scale and covers the viewport. The scene itself has black
side margins; full viewport coverage does not mean every pixel contains geometry.

| Control | Result |
|---|---|
| Cardinal 0° / 90° footprint | Bounds grow from `(958,540)-(1600,900)` to `(256,0)-(2304,1440)` |
| Cardinal beauty interior | Zero RGB differences after excluding the former boundary's 32-pixel AO support strip |
| Cardinal unlit | Every previously nonblack pixel remains RGB-identical |
| Rotated 45° beauty and unlit | Entire images RGB-identical |
| Native coverage/lifecycle tests | 27 passed, including two executed Metal allocation/readback tests |
| `render-verify.py --target IRCanvasStress --no-build --timeout 180` | All 22 checks pass; all 12 RGB reference pairs identical, strict geometry gates unchanged |

The earlier candidate wrongly fed physical backing into the sun cascade viewport.
Its rotated beauty image differs at 22,940 pixels despite identical unlit output.
`rejected-shadow-window.png` retains that discriminating control. The correction
uses the logical viewport in `beginVoxelFaceCoverage`, consistent with producer
culling and fog windows. At cardinals, the remaining beauty changes are confined
to the former backing edge: the screen-space AO stencil can now read the neighbors
and beyond-step samples that were clipped. The interior is exact.

Run `python3 docs/pr-screenshots/codex/cardinal-canvas-coverage/verify.py` to audit
the retained evidence. This is an artifact check; the runtime geometry gates are
in the CanvasStress render harness. The unlit captures precede only the final
sun-window correction and are identified separately in the manifest.

## Cost and remaining work

This is a correctness fix, not a speedup. The backing area grows about sixteenfold
for this deliberately high-density control; high-water capacity is retained.
The previous clipped cardinal timing is not equivalent rendering work and must
not serve as an optimization baseline. Next measure dispatch packing and rotating
overflow cost against complete coverage, then consider a proven screen-footprint
sample budget. Extreme density/resolution allocations still need explicit device
memory/texture-limit qualification. Native OpenGL presentation remains pending.
