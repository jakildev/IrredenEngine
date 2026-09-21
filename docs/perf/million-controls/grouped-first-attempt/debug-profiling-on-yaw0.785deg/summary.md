| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 53.017 | 52.340–53.640 |
| frame p95 | 60.193 | 57.020–66.310 |
| frame p99 | 64.903 | 59.510–72.380 |
| GPU frame commandBufferSpans | 37.372 | 37.071–37.936 |
| GPU frame envelope | 37.372 | 37.071–37.936 |
| GPU canvasClear | 0.021 | 0.020–0.021 |
| GPU computeLightVolume | 28.464 | 28.271–28.740 |
| GPU computeSunShadow | 0.051 | 0.042–0.064 |
| GPU computeVoxelAO | 0.016 | 0.015–0.016 |
| GPU computeVoxelAoPerAxis | 26.378 | 26.196–26.614 |
| GPU fbToScreen | 0.084 | 0.078–0.093 |
| GPU fogToTrixel | 0.044 | 0.041–0.046 |
| GPU lightingOverflow | 13.758 | 13.683–13.863 |
| GPU lightingPerAxis | 0.033 | 0.033–0.034 |
| GPU lightingToTrixel | 0.015 | 0.015–0.015 |
| GPU perAxisCellCompact | 0.139 | 0.115–0.151 |
| GPU perAxisScatter | 5.191 | 5.145–5.238 |
| GPU resolvePerAxisScreenDepth | 0.086 | 0.085–0.088 |
| GPU trixelToFb | 0.265 | 0.261–0.270 |
| GPU voxelCompact | 0.204 | 0.182–0.237 |
| GPU voxelPerAxisFinalize | 10.494 | 10.400–10.634 |
| GPU voxelPerAxisOverflow | 10.417 | 10.333–10.495 |
| GPU voxelPerAxisStore | 5.306 | 5.264–5.365 |
| GPU voxelSunFaces | 2.002 | 1.986–2.029 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 3.17 | 3.10–3.20 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Overflow drop warnings |
|---|---|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0 |
| 2 | True | 300 / 300 | 0 | 300 | 0 |
| 3 | True | 300 / 300 | 0 | 300 | 0 |
