| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 35.880 | 35.880–35.880 | n/a |
| frame p95 | 32.200 | 32.200–32.200 | n/a |
| frame p99 | 676.660 | 676.660–676.660 | n/a |
| steady frame avg | 25.170 | 25.170–25.170 | n/a |
| steady frame p95 | 31.220 | 31.220–31.220 | n/a |
| steady frame p99 | 32.200 | 32.200–32.200 | n/a |
| GPU frame commandBufferSpans | 7.230 | 7.230–7.230 | n/a |
| GPU frame envelope | 7.230 | 7.230–7.230 | n/a |
| GPU bakeSunShadowMap | 0.124 | 0.124–0.124 | n/a |
| GPU canvasClear | 0.030 | 0.030–0.030 | n/a |
| GPU computeLightVolume | 5.712 | 5.712–5.712 | n/a |
| GPU computeSunShadow | 0.066 | 0.066–0.066 | n/a |
| GPU computeVoxelAO | 0.047 | 0.047–0.047 | n/a |
| GPU fbToScreen | 0.032 | 0.032–0.032 | n/a |
| GPU fogToTrixel | 0.013 | 0.013–0.013 | n/a |
| GPU lightingToTrixel | 0.040 | 0.040–0.040 | n/a |
| GPU textToTrixel | 1.096 | 1.096–1.096 | n/a |
| GPU trixelToFb | 0.459 | 0.459–0.459 | n/a |
| GPU voxelCardinalElect | 1.686 | 1.686–1.686 | n/a |
| GPU voxelCompact | 0.302 | 0.302–0.302 | n/a |
| GPU voxelStage1 | 1.789 | 1.789–1.789 | n/a |
| GPU voxelStage2 | 1.671 | 1.671–1.671 | n/a |
| GPU voxelSunFaces | 0.947 | 0.947–0.947 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 25.167 | 31.224 | 32.204 | 32.204 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
