| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 60.980 | 59.860–63.050 |
| frame p95 | 71.670 | 67.060–74.690 |
| frame p99 | 78.100 | 75.640–79.780 |
| GPU frame commandBufferSpans | 44.498 | 43.783–45.685 |
| GPU frame envelope | 44.498 | 43.783–45.685 |
| GPU canvasClear | 0.021 | 0.019–0.023 |
| GPU computeLightVolume | 33.410 | 32.895–34.044 |
| GPU computeSunShadow | 0.054 | 0.052–0.056 |
| GPU computeVoxelAO | 0.017 | 0.016–0.018 |
| GPU computeVoxelAoPerAxis | 30.698 | 30.241–31.267 |
| GPU fbToScreen | 0.110 | 0.085–0.126 |
| GPU fogToTrixel | 0.049 | 0.046–0.051 |
| GPU lightingOverflow | 15.369 | 15.157–15.669 |
| GPU lightingPerAxis | 0.043 | 0.040–0.045 |
| GPU lightingToTrixel | 0.021 | 0.019–0.022 |
| GPU perAxisCellCompact | 0.177 | 0.176–0.180 |
| GPU perAxisScatter | 6.854 | 6.695–7.149 |
| GPU resolvePerAxisScreenDepth | 0.105 | 0.101–0.108 |
| GPU trixelToFb | 0.286 | 0.278–0.300 |
| GPU voxelCompact | 0.211 | 0.207–0.215 |
| GPU voxelPerAxisFinalize | 11.093 | 10.975–11.311 |
| GPU voxelPerAxisOverflow | 13.336 | 13.097–13.581 |
| GPU voxelPerAxisStore | 6.044 | 5.990–6.135 |
| GPU voxelSunFaces | 2.397 | 2.353–2.459 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 3.67 | 3.60–3.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Overflow drop warnings |
|---|---|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0 |
| 2 | True | 300 / 300 | 0 | 300 | 0 |
| 3 | True | 300 / 300 | 0 | 300 | 0 |
