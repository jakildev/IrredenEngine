# Independent parent-reference visual review

Compared the exact finite, unmasked scatter capture (HEAD `02ad32f2` plus the dirty shader snapshot, binary SHA `e5a90670`) against the **eight macos-debug CanvasStress references committed at PR3941 parent `02ad32f2`**. The six default references map to shots 0–5; `compare_yaw0` and `compare_yaw_q` map to `--only compare` shots 12–13. These reference PNGs are byte-identical to the retained postmerge full-scene evidence for those eight shots. Per-frame exact RGB counts and capture/reference SHA-256 values are in [visual-review-stack-index.json](visual-review-stack-index.json).

| Capture set | Frames | Changed RGB pixels | Reference color → black | Black → final color | Color → color | Newly enclosed black pixels (4-neighbor) |
|---|---:|---:|---:|---:|---:|---:|
| Default | 6 | 13,176 | 2,824 | 8 | 10,344 | 0 |
| Compare | 2 | 340 | 180 | 0 | 160 | 0 |

| Parent reference | Final shot | Changed | To black | Added | Recolored | Max channel delta |
|---|---:|---:|---:|---:|---:|---:|
| `so3_smooth_sweep` | default 0 | 2,744 | 480 | 0 | 2,264 | 182 |
| `so3_offsnap_disc` | default 1 | 1,668 | 504 | 0 | 1,164 | 182 |
| `so3_wide_parity` | default 2 | 2,324 | 472 | 0 | 1,852 | 182 |
| `so3_grid_cross` | default 3 | 1,736 | 356 | 0 | 1,380 | 182 |
| `so3_offsnap_wide` | default 4 | 2,044 | 452 | 8 | 1,584 | 182 |
| `revoxelize_solids` | default 5 | 2,660 | 560 | 0 | 2,100 | 182 |
| `compare_yaw0` | compare 12 | 292 | 132 | 0 | 160 | 178 |
| `compare_yaw_q` | compare 13 | 48 | 48 | 0 | 0 | 173 |

Viewed paired contact sheets for all eight references and final captures, then full-frame default shot 0 and compare shot 12 plus full-resolution paired crops of default shots 0/3 and compare shots 12/13. The visible scene objects remain present. Default differences cluster at ridges and face-color boundaries; compare differences are small and lie on the GRID cube perimeter. No removed-to-black pixel in the eight shots is enclosed on four sides by nonblack pixels, and the inspected crops show no conspicuous new interior hole or wrong-facing face. Existing ribbed/carved source forms remain visible in both versions. These observations support refreshing the eight references after the separate geometric oracle and native checks; they do not by themselves prove pixelwise geometric correctness.

**Baseline attribution:** The older e10 integrated captures are *not* the PR3941 parent. Their merge base with `02ad32f2` is `d29d1cce`, and eight lighting/face-selection shader paths differ, including `ir_voxel_face_select` in both backends. The earlier 27-frame e10 comparison therefore cannot isolate this scatter slice and is excluded from these counts. The other 19 full-scene frames lack a same-parent retained baseline in this report.

Simplify scan of the changed scatter shaders and `engine/video/src/auto_screenshot.cpp`: no added grid/tick loop or backend primitive outside `engine/render/`; no renderer leak. The only new nontrivial helper is `logCaptureCameraState`, which calls camera getters and logs at the two screenshot request sites. Its call sequence has no equivalent helper to reuse; `logCullValidateState` is a separate, scene-specific cull oracle rather than the same operation. No source edit was made for this review.
