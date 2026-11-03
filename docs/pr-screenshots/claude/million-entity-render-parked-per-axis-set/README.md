# Parked per-axis set — captures

Two capture sets, both on macos-debug-class hosts (Metal), full-size PNGs.

## Pixel identity on the million scene (Release, `IRPerfGrid`)

One file per distinct SHA-256; the arms that produced each are listed in
[`docs/perf/continuous-yaw-sweep/parked-identity/README.md`](../../../perf/continuous-yaw-sweep/parked-identity/README.md).

| File | SHA-256 (first 16) | Produced by |
|---|---|---|
| `cardinal-0deg-parked-set-resident.png` | `d145106255ee8f8d` | the cardinal frame with the set parked; identical to the never-allocated frame and to master's frame after its release |
| `rotated-91p2deg-unparked-and-settled.png` | `34449f70093ec346` | the first frame on the unparked set (frame 77 of the sweep); identical to 91.2° held from frame 1 |
| `rotated-91p2deg-control-first-frame-after-reallocation.png` | `8e1b1ab979d07c2f` | master's frame 77, the first on a fresh allocation |
| `rotated-91p2deg-control-vs-parked-diff.png` | — | `render-compare.py --per-pixel-tol 0 --diff-out` of the two 91.2° frames: 32,300 of 3,686,400 pixels differ by 1 to 7, 304 by 32 to 54 |

## Nine-yaw `IRCanvasStress` set (Debug, before and after)

`IRCanvasStress --auto-screenshot 120 --pivot-origin --no-spin
--no-auto-rotate --sweep-yaw 0 6.2831853 9`, run once on origin/master
`79ca9e3c6` and once on this tree, 60 settle frames a pose, so each cardinal
pose is reached from a rotated one (the set parks) and each rotated pose from
a cardinal (the set unparks, inside the 120-frame window). Every pair is
byte-identical, so one file stands for both:

| File | Yaw | SHA-256 before | SHA-256 after |
|---|---:|---|---|
| `nineyaw_000deg-before-and-after.png` | 0° | `85468d985f1c0e4b` | `85468d985f1c0e4b` |
| `nineyaw_045deg-before-and-after.png` | 45° | `7600b29825212675` | `7600b29825212675` |
| `nineyaw_090deg-before-and-after.png` | 90° | `71a2c93afc3b9d48` | `71a2c93afc3b9d48` |
| `nineyaw_135deg-before-and-after.png` | 135° | `d750e6c93782bf3d` | `d750e6c93782bf3d` |
| `nineyaw_180deg-before-and-after.png` | 180° | `ce8b4dc6297bc94b` | `ce8b4dc6297bc94b` |
| `nineyaw_225deg-before-and-after.png` | 225° | `5d6e8c3fc2687ed9` | `5d6e8c3fc2687ed9` |
| `nineyaw_270deg-before-and-after.png` | 270° | `341270093a6c5aa6` | `341270093a6c5aa6` |
| `nineyaw_315deg-before-and-after.png` | 315° | `c869dddde7b3755e` | `c869dddde7b3755e` |
| `nineyaw_360deg-before-and-after.png` | 360° | `062c47d61db5ed47` | `062c47d61db5ed47` |
