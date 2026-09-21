| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 42.370 | 38.210–45.000 |
| frame p95 | 56.030 | 51.230–61.850 |
| frame p99 | 63.233 | 54.620–70.140 |
| GPU frame commandBufferSpans | 29.366 | 25.697–31.630 |
| GPU frame envelope | 29.366 | 25.697–31.630 |
| GPU canvasClear | 0.030 | 0.022–0.037 |
| GPU computeLightVolume | 7.710 | 6.672–8.255 |
| GPU computeSunShadow | 0.041 | 0.036–0.044 |
| GPU computeVoxelAO | 0.068 | 0.059–0.074 |
| GPU fbToScreen | 0.050 | 0.041–0.055 |
| GPU fogToTrixel | 0.015 | 0.012–0.017 |
| GPU lightingToTrixel | 0.044 | 0.039–0.047 |
| GPU trixelToFb | 0.307 | 0.294–0.325 |
| GPU voxelCompact | 0.256 | 0.220–0.292 |
| GPU voxelStage1 | 10.164 | 8.750–11.031 |
| GPU voxelStage2 | 7.176 | 6.176–7.824 |
| GPU voxelSunFaces | 2.598 | 2.343–2.754 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.53 | 2.30–2.70 | 6 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Overflow drop warnings |
|---|---|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0 |
| 2 | True | 300 / 300 | 0 | 300 | 0 |
| 3 | True | 300 / 300 | 0 | 300 | 0 |
