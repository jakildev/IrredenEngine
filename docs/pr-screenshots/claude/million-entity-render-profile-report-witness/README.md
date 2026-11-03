# Pixel-identity control for the run witness

The change adds two CPU-side recordings to `VOXEL_TO_TRIXEL_STAGE_1` (the yaw
it renders at in `beginTick`, the overflow ctrl block it already reads) and
issues no GPU call, so a before/after pair is one image twice. The control is
the byte comparison instead.

`IRCanvasStress --auto-screenshot 120`, macos-debug, Metal, the default
twelve-shot table, captured twice from one tree: once as committed and once
with both `renderRunWitness()` calls commented out and `IRCanvasStress`
rebuilt. SHA-256 of each PNG, first sixteen hex digits:

| Shot | With the recording | Without it | |
|---:|---|---|---|
| 1 | `7882f175bdf96a67` | `7882f175bdf96a67` | identical |
| 2 | `f9c5d1b5ea6be168` | `f9c5d1b5ea6be168` | identical |
| 3 | `5ebd3c67b492a3e4` | `5ebd3c67b492a3e4` | identical |
| 4 | `7c1badb8ee3349c8` | `7c1badb8ee3349c8` | identical |
| 5 | `217603962534067a` | `217603962534067a` | identical |
| 6 | `7282bac7db3a6628` | `7282bac7db3a6628` | identical |
| 7 | `35d5dffb6440232c` | `35d5dffb6440232c` | identical |
| 8 | `9690f50267d0727d` | `9690f50267d0727d` | identical |
| 9 | `e0a56cde73fa8312` | `e0a56cde73fa8312` | identical |
| 10 | `efaac5e8d7d3bab6` | `efaac5e8d7d3bab6` | identical |
| 11 | `af38a07416158548` | `af38a07416158548` | identical |
| 12 | `94d0204cf8388be1` | `94d0204cf8388be1` | identical |

Twelve of twelve byte-identical. `canvas-stress-shot-06.png` is shot 6 of
either arm.

`render-verify --target IRShapeDebug`: all 33 checks pass with a maximum
delta of 0. `render-verify --target IRCanvasStress` fails 7 of 11 against the
committed references in both arms; that is the stale macos-debug reference
set on master tracked in issue 3622, not this change.
