| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.870 | 32.870–32.870 | n/a |
| frame p95 | 29.010 | 29.010–29.010 | n/a |
| frame p99 | 691.850 | 691.850–691.850 | n/a |
| steady frame avg | 21.990 | 21.990–21.990 | n/a |
| steady frame p95 | 25.280 | 25.280–25.280 | n/a |
| steady frame p99 | 29.010 | 29.010–29.010 | n/a |
| GPU frame commandBufferSpans | 5.090 | 5.090–5.090 | n/a |
| GPU frame envelope | 5.090 | 5.090–5.090 | n/a |
| GPU bakeSunShadowMap | 0.092 | 0.092–0.092 | n/a |
| GPU canvasClear | 0.074 | 0.074–0.074 | n/a |
| GPU computeLightVolume | 2.648 | 2.648–2.648 | n/a |
| GPU computeSunShadow | 0.069 | 0.069–0.069 | n/a |
| GPU computeVoxelAO | 0.058 | 0.058–0.058 | n/a |
| GPU fbToScreen | 0.125 | 0.125–0.125 | n/a |
| GPU fogToTrixel | 0.033 | 0.033–0.033 | n/a |
| GPU lightingToTrixel | 0.059 | 0.059–0.059 | n/a |
| GPU textToTrixel | 1.316 | 1.316–1.316 | n/a |
| GPU trixelToFb | 1.170 | 1.170–1.170 | n/a |
| GPU voxelCardinalElect | 0.469 | 0.469–0.469 | n/a |
| GPU voxelCompact | 0.301 | 0.301–0.301 | n/a |
| GPU voxelStage1 | 0.581 | 0.581–0.581 | n/a |
| GPU voxelStage2 | 0.466 | 0.466–0.466 | n/a |
| GPU voxelSunFaces | 1.099 | 1.099–1.099 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 21.989 | 25.277 | 29.012 | 29.012 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
