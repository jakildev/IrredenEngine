| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 37.870 | 37.870–37.870 | n/a |
| frame p95 | 35.170 | 35.170–35.170 | n/a |
| frame p99 | 673.940 | 673.940–673.940 | n/a |
| steady frame avg | 28.040 | 28.040–28.040 | n/a |
| steady frame p95 | 33.560 | 33.560–33.560 | n/a |
| steady frame p99 | 35.170 | 35.170–35.170 | n/a |
| GPU frame commandBufferSpans | 7.374 | 7.374–7.374 | n/a |
| GPU frame envelope | 7.374 | 7.374–7.374 | n/a |
| GPU bakeSunShadowMap | 0.137 | 0.137–0.137 | n/a |
| GPU canvasClear | 0.045 | 0.045–0.045 | n/a |
| GPU computeLightVolume | 5.926 | 5.926–5.926 | n/a |
| GPU computeSunShadow | 0.058 | 0.058–0.058 | n/a |
| GPU computeVoxelAO | 0.049 | 0.049–0.049 | n/a |
| GPU fbToScreen | 0.035 | 0.035–0.035 | n/a |
| GPU fogToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU lightingToTrixel | 0.043 | 0.043–0.043 | n/a |
| GPU textToTrixel | 0.985 | 0.985–0.985 | n/a |
| GPU trixelToFb | 0.566 | 0.566–0.566 | n/a |
| GPU voxelCardinalElect | 1.714 | 1.714–1.714 | n/a |
| GPU voxelCompact | 0.238 | 0.238–0.238 | n/a |
| GPU voxelStage1 | 1.825 | 1.825–1.825 | n/a |
| GPU voxelStage2 | 1.709 | 1.709–1.709 | n/a |
| GPU voxelSunFaces | 0.941 | 0.941–0.941 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 28.037 | 33.556 | 35.167 | 35.167 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.70 | 1.70–1.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
