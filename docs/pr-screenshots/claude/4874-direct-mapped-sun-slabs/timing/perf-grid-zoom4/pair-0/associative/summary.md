| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 38.220 | 38.220–38.220 | n/a |
| frame p95 | 35.340 | 35.340–35.340 | n/a |
| frame p99 | 672.680 | 672.680–672.680 | n/a |
| steady frame avg | 28.200 | 28.200–28.200 | n/a |
| steady frame p95 | 33.990 | 33.990–33.990 | n/a |
| steady frame p99 | 35.340 | 35.340–35.340 | n/a |
| GPU frame commandBufferSpans | 7.225 | 7.225–7.225 | n/a |
| GPU frame envelope | 7.225 | 7.225–7.225 | n/a |
| GPU bakeSunShadowMap | 0.119 | 0.119–0.119 | n/a |
| GPU canvasClear | 0.045 | 0.045–0.045 | n/a |
| GPU computeLightVolume | 5.819 | 5.819–5.819 | n/a |
| GPU computeSunShadow | 0.058 | 0.058–0.058 | n/a |
| GPU computeVoxelAO | 0.049 | 0.049–0.049 | n/a |
| GPU fbToScreen | 0.035 | 0.035–0.035 | n/a |
| GPU fogToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU lightingToTrixel | 0.041 | 0.041–0.041 | n/a |
| GPU textToTrixel | 1.001 | 1.001–1.001 | n/a |
| GPU trixelToFb | 0.543 | 0.543–0.543 | n/a |
| GPU voxelCardinalElect | 1.676 | 1.676–1.676 | n/a |
| GPU voxelCompact | 0.292 | 0.292–0.292 | n/a |
| GPU voxelStage1 | 1.743 | 1.743–1.743 | n/a |
| GPU voxelStage2 | 1.733 | 1.733–1.733 | n/a |
| GPU voxelSunFaces | 0.932 | 0.932–0.932 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 28.200 | 33.989 | 35.338 | 35.338 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.70 | 1.70–1.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
