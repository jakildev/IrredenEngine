# Editor workflow UI evidence

Host: macOS, Metal, Debug, 3200×1800 framebuffer. Before: parent commit
`e2e7c778f93891ee294544bba8d463b3498c8edc`. After: this change.

`sculpt-before.png` and `sculpt-after.png` show the same first frame of
`sculpt_workflow`. The fixture hides the document bar; ordinary authoring uses
the compact name/save/open/folder bar. `document-confirmation.png` shows that
bar with its confirmation modal.

- `palette-add.png`: newly appended custom color, second swatch page.
- `radial-preview.png`: two prospective copies at radius 8, yaw 30 degrees.
- `lod-comparison.png`: real five-zoom capture, common aperture and onion skin.
  The intentionally smaller blue coarse variant exposes a size mismatch.
  Empty intermediate tiers are deliberate. Native tiles say CROP when the
  image exceeds their display area; common-scale tiles retain shared alignment.

## Validation

All runs use `python3 scripts/gui-verify.py --no-build IRVoxelEditor --
--gui-session NAME --gui-layout-check`. The capture wrapper preserves unrelated
user screenshots and copies only this run's output.

| Session | Assertions | Layout findings |
|---|---:|---:|
| sculpt_workflow | 46/46 | 0 |
| palette_workflow | 8/8 | 0 |
| array_workflow | 10/10 | 0 |
| document_workflow | 21/21 | 0 |
| hierarchy_workflow | 18/18 | 0 |
| local_canvas | 10/10 | 0 |
| workarea_focus | 5/5 | 0 |
| lod_workflow | 9/9 | 0 |
| motion_preview | 11/11 | 0 |

Debug and Release `IRVoxelEditor` builds succeeded. The nine native sessions
passed 138 assertions across 106 captures with zero layout findings.

`VoxelEditor*:ComparisonImage.*`: 100/100 unit tests, including 28 new palette,
array and LOD/image checks. Header checks scanned 808 headers; comment-reference
lint and diff whitespace checks passed.

Array checks use real checkbox/slider/Apply/Undo gestures at 30 and 67 degrees,
compare every committed exposed face with the retained preview, and check the
source fingerprint and pre-Apply history. LOD checks inspect renderer capture
texels, exact opacity endpoints, source fingerprints, same-extent tier copies,
Undo and view restoration. Wheel/right-drag during acquisition must yield five
byte-identical images to a subsequent clean capture.

## Bounds and follow-up

The layout targets at least 1200×800 GUI texels; adaptive docking is separate.
Palettes use RGB values and GIMP `.gpl`, independent of document saving. Array
proposals copy the selected part only; SDF/detached proposals are labeled voxel
approximations. LOD captures are bounded and refreshed explicitly. Copy To Tier
preserves authored cells rather than automatically decimating; overlapping
source bands remain the author's explicit choice. Native physical-size tiles
require integer uniform GUI scaling. OpenGL runtime captures were not run here.
