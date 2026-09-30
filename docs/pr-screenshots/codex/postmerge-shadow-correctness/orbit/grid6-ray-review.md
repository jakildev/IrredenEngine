# Focused GRID orbit-6 face-owner check

Native fixture at head `0ac961ac2b48dfe2ac3495a01ab2c3fc97111ac7`: `IRCanvasStress --only orbit --focus-orbit 6 --no-spin --no-auto-rotate --pivot-origin --no-ao --debug-overlay normals --zoom 4 --subdivisions 2 --sweep-yaw 2.35619449 2.35619449 1 --auto-screenshot 6`. Both screenshots are 2560×1440 Metal output. The A/B changes only the shader face-selection predicate so the `kRotatedEmit` riser rule does not apply to per-axis routes.

The independent [ray-check script](grid6-ray-check.py) reconstructs the 12³ authored cube centers, rotates its occupancy by 45° around Y through inverse destination-cell sampling with `floor(v+0.5)`, and intersects the camera ray with the resulting unit boxes. It derives 1,740 occupied cells and 610 surface cells, exactly matching the native log. The 12 cells omitted by the pool cap are interior. At camera yaw 135°, the world ray has zero Y component, so every interior image column should have the same frontmost X/Z face sequence. The physical alternating +X and -Z bands are expected.

Before the A/B, the normal overlay has 46,864 +X pixels, 27,904 -Z pixels, and 2,928 +Z pixels. `render-normal-facing-metric.py --yaw 135` reports 2,928 back-facing pixels. The blue +Z pixels occupy x=1202..1405,y=808..873. The ray at (1280,840) enters the +X face of world cell (7,0,2), about 4.4 screen pixels from its nearest Z edge, but the native overlay reports +Z. The full-window comparison counts 2,814 +X→+Z wrong-face pixels more than one pixel from expected face/silhouette boundaries. The +Z presentation is therefore a real face-owner defect, not a legitimate stair face.

After the A/B, 2,828 blue pixels become the expected +X pink; 100 blue pixels become black at the right/bottom silhouette, and 456 pink pixels become black on the bottom two raster rows. The normal-facing gate reports zero back-facing pixels among 77,140 occupied. The independent full-window check reports zero missing or wrong-face interior pixels. Its remaining 218 interior-labeled mismatches are extra pixels at the right raster silhouette, where this simple ray-center expected image disagrees with the renderer's edge convention; the rest fall inside the one-pixel face/silhouette band. This single-pose result supports the route-scoped change but does not establish all yaw and density cases.

The fixed-pose ray check can be rerun from the repository root:

```sh
python3 docs/pr-screenshots/codex/postmerge-shadow-correctness/orbit/grid6-ray-check.py docs/pr-screenshots/codex/postmerge-shadow-correctness/orbit/normals/shot-0.png
python3 docs/pr-screenshots/codex/postmerge-shadow-correctness/orbit/grid6-ray-check.py docs/pr-screenshots/codex/postmerge-shadow-correctness/orbit/fix-normals/shot-0.png
python3 scripts/render-normal-facing-metric.py docs/pr-screenshots/codex/postmerge-shadow-correctness/orbit/fix-normals/shot-0.png --yaw 135
```

Machine evidence: [before ray results](normals/grid6-ray-check.json), [after ray results](fix-normals/grid6-ray-check.json), and both `shot-0.png` images in those directories. The normal-facing gate is already in the repository. This fixed-pose ray checker is diagnostic evidence and has not been added to production tests.
