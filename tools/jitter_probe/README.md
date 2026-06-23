# jitter_probe

Temporal-jitter detector for render-verification sweeps. The sibling of
[`tools/img_diff`](../img_diff/README.md): `img_diff` catches **spatial** drift
between two frames; `jitter_probe` catches **temporal** jitter across a
**sequence** of frames captured while the camera moves smoothly.

## Why it exists

A correct render pipeline translates a shape **smoothly** as the camera pans or
yaws: the shape's centroid follows a straight line, so the per-frame delta keeps
one sign and the residual off that line stays sub-pixel. A jittering pipeline —
e.g. an integer canvas anchor whose sub-pixel compensation is at the wrong
screen scale (#1944) — makes the centroid **oscillate**: the delta reverses sign
and the residual spikes, even though every individual frame looks fine. That
oscillation is **invisible in any single screenshot**; it only shows up across
the sequence. A still image can't prove temporal stability — this can.

## Usage

```
jitter_probe <frame_0.png> <frame_1.png> ... <frame_N.png>   # >=3, in capture order
    [--threshold L]      foreground = pixels with (R+G+B) > L (0..765, default 24)
    [--color R,G,B,T]    instead, foreground = pixels within T of color R,G,B
    [--reversal-eps PX]  per-frame deltas under this count as 0 (default 0.10)
    [--max-residual PX]  SMOOTH verdict requires residual <= this (default 1.50)
    [--verbose]          print the per-frame centroid + residual table
```

Exit code: `0` = SMOOTH, `1` = JITTER detected, `2` = argument / IO error (same
convention as `img_diff`, so it drops into the same verification scripts).

## Capturing a clean sweep

Use an **isolated shape on a black field** so the centroid is uncontaminated.
`shape_debug` has two sweep harnesses (see
[`engine/render/CLAUDE.md`](../../engine/render/CLAUDE.md) §"Verifying temporal
stability (jitter)"):

```bash
# Pan jitter — camera pans at a fixed non-cardinal yaw:
IRShapeDebug --spin-shape box --spin-shape-voxel --pan-sweep --yaw 0.785 --zoom 4 \
    --auto-screenshot 6
# Rotation jitter — camera yaws within one cardinal quadrant (use a vertical
# cylinder: its silhouette is Z-yaw-invariant, so any centroid wobble is jitter):
IRShapeDebug --spin-shape cylinder --spin-shape-voxel --yaw-sweep --zoom 4 \
    --auto-screenshot 6

# Then point jitter_probe at the captured sequence (in order):
build/tools/jitter_probe/jitter_probe save_files/screenshots/screenshot_0001*.png
```

For a multi-shape scene with no isolation, pass `--color R,G,B,T` to lock onto
one shape. A static-determinism check ("does it jitter after the camera stops?")
is a separate thing — capture the SAME pose twice and `img_diff` them (expect 0).

## Interpreting output

```
jitter_probe: frames=24 (valid=24)  verdict=SMOOTH
  x: reversals=0  max_residual=0.20px  delta_std=0.41  delta_max=1.00
  y: reversals=0  max_residual=0.31px  delta_std=0.30  delta_max=0.51
```

`reversals` is the count of per-frame direction flips (the jitter signature);
`max_residual` is the worst deviation from the smooth straight-line motion.
SMOOTH requires `reversals == 0` on both axes and `max_residual <= --max-residual`.
A clean fix flips a JITTER verdict (high reversals, multi-px residual) to SMOOTH
(0 reversals, sub-px residual).
