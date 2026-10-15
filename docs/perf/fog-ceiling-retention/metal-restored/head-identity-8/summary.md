| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 19.000 | 19.000–19.000 |
| frame p95 | 20.660 | 20.660–20.660 |
| frame p99 | 21.060 | 21.060–21.060 |
| steady frame avg | 18.800 | 18.800–18.800 |
| steady frame p95 | 20.620 | 20.620–20.620 |
| steady frame p99 | 20.960 | 20.960–20.960 |
| GPU frame commandBufferSpans | 4.445 | 4.445–4.445 |
| GPU frame envelope | 4.445 | 4.445–4.445 |
| GPU canvasClear | 0.050 | 0.050–0.050 |
| GPU computeLightVolume | 2.455 | 2.455–2.455 |
| GPU computeSunShadow | 0.058 | 0.058–0.058 |
| GPU computeVoxelAO | 0.053 | 0.053–0.053 |
| GPU fbToScreen | 0.121 | 0.121–0.121 |
| GPU fogToTrixel | 0.030 | 0.030–0.030 |
| GPU lightingToTrixel | 0.047 | 0.047–0.047 |
| GPU textToTrixel | 0.891 | 0.891–0.891 |
| GPU trixelToFb | 0.250 | 0.250–0.250 |
| GPU voxelCardinalElect | 0.478 | 0.478–0.478 |
| GPU voxelCompact | 0.350 | 0.350–0.350 |
| GPU voxelStage1 | 0.556 | 0.556–0.556 |
| GPU voxelStage2 | 0.473 | 0.473–0.473 |
| GPU voxelSunFaces | 1.015 | 1.015–1.015 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 18.802 | 20.620 | 20.957 | 20.989 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.10 | 1.10–1.10 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
