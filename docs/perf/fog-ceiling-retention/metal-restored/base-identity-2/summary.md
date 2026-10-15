| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 18.930 | 18.930–18.930 |
| frame p95 | 22.440 | 22.440–22.440 |
| frame p99 | 25.120 | 25.120–25.120 |
| steady frame avg | 19.040 | 19.040–19.040 |
| steady frame p95 | 22.290 | 22.290–22.290 |
| steady frame p99 | 24.700 | 24.700–24.700 |
| GPU frame commandBufferSpans | 4.117 | 4.117–4.117 |
| GPU frame envelope | 4.117 | 4.117–4.117 |
| GPU canvasClear | 0.048 | 0.048–0.048 |
| GPU computeLightVolume | 2.258 | 2.258–2.258 |
| GPU computeSunShadow | 0.056 | 0.056–0.056 |
| GPU computeVoxelAO | 0.049 | 0.049–0.049 |
| GPU fbToScreen | 0.112 | 0.112–0.112 |
| GPU fogToTrixel | 0.028 | 0.028–0.028 |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 |
| GPU textToTrixel | 0.881 | 0.881–0.881 |
| GPU trixelToFb | 0.270 | 0.270–0.270 |
| GPU voxelCardinalElect | 0.436 | 0.436–0.436 |
| GPU voxelCompact | 0.353 | 0.353–0.353 |
| GPU voxelStage1 | 0.513 | 0.513–0.513 |
| GPU voxelStage2 | 0.430 | 0.430–0.430 |
| GPU voxelSunFaces | 0.937 | 0.937–0.937 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 19.042 | 22.293 | 24.696 | 26.067 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.10 | 1.10–1.10 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
