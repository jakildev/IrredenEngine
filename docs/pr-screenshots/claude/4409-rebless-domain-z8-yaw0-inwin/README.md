# `domain_z8_yaw0_inwin` re-bless: capture provenance

Row: `light-domain-matrix/domain_z8_yaw0_inwin` of `scripts/light-verify.py`
(zoom 8, yaw 0, camera `(18,30)`, game resolution 1280 x 720).

Host: Apple M4 Max, Metal, macOS 26.5.2, offscreen window, 2560 x 1440
framebuffer. Every capture came from a fresh `fleet-build --target
IRLightingEmissive` of the named tree and this command:

```
fleet-run --timeout 120 IRLightingEmissive --light-domain-matrix --auto-screenshot 10
```

The row is shot 28 of 36 (`screenshot_000028.png`), mapped from the run's
`AutoScreenshot 28/36: domain_z8_yaw0_inwin` and `Saved screenshot` log lines.

| Tree | Capture SHA-256 (first 12) | Against the pre-change reference |
|---|---|---|
| `b834ca946`, parent of the moving commit | `bf8d85148fb2` | 100.0 %, max delta 0, identical |
| `957142947`, the moving commit (#4363) | `e6fcc21a5878` | 99.4535 %, max delta 124, PSNR 33.67 dB, local |
| `8a1bc317a` with `git revert -n 957142947` | `bf8d85148fb2` | 100.0 %, max delta 0, identical |
| `8a1bc317a`, master at capture time (twice) | `e6fcc21a5878` | 99.4535 %, max delta 124, PSNR 33.67 dB, local |

Files:

- `domain_z8_yaw0_inwin-before.png`: the reference as committed before this
  change, byte for byte (`bf8d85148fb2`).
- `domain_z8_yaw0_inwin-after.png`: the new reference, byte for byte
  (`e6fcc21a5878`).
- `domain_z8_yaw0_inwin-diff-x4.png`: per-channel absolute difference of the
  two, multiplied by 4 and clipped.
- `crop-<subject>-{before,after,diff-x4}.png`: the three boxes that hold all
  30 640 differing pixels, scaled 2x nearest-neighbour. Boxes in full-frame
  pixels: blue voxel ring `(16,352)-(624,692)`, orange voxel solid
  `(700,670)-(964,804)`, yellow voxel ball `(1132,766)-(1556,1124)`.

Alpha is 255 at every pixel of both frames, so the RGB difference is the whole
difference.
