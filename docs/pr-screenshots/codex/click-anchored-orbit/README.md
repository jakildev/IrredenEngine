# Click-anchored orbit evidence

Captured on macOS / Apple M4 Max / Metal using the Debug editor. Release
was also built. These captures exercise native input and framebuffer readback.
No lighting-quality or frame-time claim is made by the unlit orbit fixture.

## Reproduction

```sh
fleet-build --target IRVoxelEditor IrredenEngineTest IRShapeDebug jitter_probe
python3 scripts/gui-verify.py --no-build IRVoxelEditor -- --gui-session click_orbit --gui-layout-check
python3 scripts/gui-verify.py --no-build IRVoxelEditor -- --gui-session click_orbit --gui-layout-check --config-preset test/alignment_letterbox.lua
python3 scripts/gui-verify.py --no-build IRVoxelEditor -- --gui-session camera_gestures --gui-layout-check
python3 scripts/gui-verify.py --no-build IRVoxelEditor -- --gui-session region_tools --gui-layout-check
python3 scripts/pivot-verify.py --no-build --blocks cursor-latch
```

The normal window is 1600×900 logical points / 3200×1800 framebuffer pixels,
with a 1600×900 game canvas at FIT scale 2. The letterboxed window is
1200×800 points / 2400×1600 pixels with the same game canvas at FIT scale 1.
The orbit fixture contains a 16³ authoring canvas with either 27 red unlit
voxels or an empty floor. Zoom is 4 except for the explicit zoom prelude.
It tests initial yaws 0°, 30°, 67°, 90°, 120°, 240°, and 270°.

Each solid/floor case captures baseline, press, held rotation, release, and
idle. The 30° solid case uses Alt+left after panning, the 67° solid case
uses Ctrl+middle after zooming, and the others use right-drag. The fixture
also reanchors after rotation and pans with Space after orbiting.

## Results

- Normal and letterboxed orbit sessions: **70/70 each**, no layout findings.
- Existing camera gestures: **44/44**, no layout findings.
- Box/line toolbar, preview, commit, Undo, and Escape: **25/25**, no layout findings.
- Focus/reset/pan/marker/save-registry unit filter: **68 passed**.
- Legacy cursor-latch compatibility: **FOCUS-OK**, 9/9 analytic focus checks.
  Its silhouette centroid is diagnostic rather than gated (16 px deviation);
  it is not evidence that a rotating silhouette stays stationary.
- Header/component inventory, formatting, and comment-reference checks passed.

The orbit fixture chooses solid clicks from rendered red interior pixels,
then independently checks the projected world anchor and the gold marker's
actual framebuffer centroid. Final marker error and press-to-held drift are
both **0 physical pixels** in both window configurations. Geometry, history,
and view continuity are checked separately; a stationary overlay alone cannot
make the camera checks pass. Filtered assertion rows accompany these images.

## Images

`right-orbit-before.png` and `right-orbit-after.png` are the same held-right
gesture from the existing camera session at zoom 8. The before binary was
built from parent commit `01a7ef1f14035b4bc289731dc38f0426fca00fe0`; the after
binary contains this change. The gold cross appears at the clicked surface.

`yaw30-solid-*` and `yaw67-floor-*` show press and held rotation from the
new session. The `-detail` files are 400×300 pixel crops centered on the
same measured marker point, without resizing. Full-frame originals are
included. The cross has a 6-point radius and 2-point bar width.

A native test exposed Space triggering both pan and camera reset; reset is
now Home. An early SDF marker passed projection math but jittered in actual
pixels. The final editor marker uses the existing screen overlay, and the
pixel-centroid assertions gate that regression.

Clip capture was skipped because this editor has no auto-record wiring.
