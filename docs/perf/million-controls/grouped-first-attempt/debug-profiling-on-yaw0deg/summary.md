| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 33.700 | 33.460–33.930 |
| frame p95 | 36.113 | 35.750–36.490 |
| frame p99 | 40.087 | 38.760–42.530 |
| GPU frame commandBufferSpans | 21.787 | 21.663–21.884 |
| GPU frame envelope | 21.787 | 21.663–21.884 |
| GPU canvasClear | 0.024 | 0.023–0.026 |
| GPU computeLightVolume | 5.620 | 5.409–5.946 |
| GPU computeSunShadow | 0.031 | 0.030–0.032 |
| GPU computeVoxelAO | 0.052 | 0.050–0.054 |
| GPU fbToScreen | 0.039 | 0.037–0.040 |
| GPU fogToTrixel | 0.010 | 0.010–0.011 |
| GPU lightingToTrixel | 0.033 | 0.032–0.034 |
| GPU trixelToFb | 0.235 | 0.225–0.244 |
| GPU voxelCompact | 0.192 | 0.181–0.209 |
| GPU voxelStage1 | 7.368 | 7.306–7.430 |
| GPU voxelStage2 | 5.154 | 5.127–5.168 |
| GPU voxelSunFaces | 2.026 | 2.022–2.029 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.00 | 2.00–2.00 | 6 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Overflow drop warnings |
|---|---|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0 |
| 2 | True | 300 / 300 | 0 | 300 | 0 |
| 3 | True | 300 / 300 | 0 | 300 | 0 |
