| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 19.080 | 19.080–19.080 |
| frame p95 | 22.450 | 22.450–22.450 |
| frame p99 | 24.350 | 24.350–24.350 |
| steady frame avg | 19.350 | 19.350–19.350 |
| steady frame p95 | 22.640 | 22.640–22.640 |
| steady frame p99 | 23.950 | 23.950–23.950 |
| GPU frame commandBufferSpans | 3.982 | 3.982–3.982 |
| GPU frame envelope | 3.982 | 3.982–3.982 |
| GPU canvasClear | 0.051 | 0.051–0.051 |
| GPU computeLightVolume | 2.190 | 2.190–2.190 |
| GPU computeSunShadow | 0.051 | 0.051–0.051 |
| GPU computeVoxelAO | 0.047 | 0.047–0.047 |
| GPU fbToScreen | 0.105 | 0.105–0.105 |
| GPU fogToTrixel | 0.026 | 0.026–0.026 |
| GPU lightingToTrixel | 0.043 | 0.043–0.043 |
| GPU textToTrixel | 0.710 | 0.710–0.710 |
| GPU trixelToFb | 0.304 | 0.304–0.304 |
| GPU voxelCardinalElect | 0.419 | 0.419–0.419 |
| GPU voxelCompact | 0.327 | 0.327–0.327 |
| GPU voxelStage1 | 0.496 | 0.496–0.496 |
| GPU voxelStage2 | 0.414 | 0.414–0.414 |
| GPU voxelSunFaces | 0.881 | 0.881–0.881 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 19.346 | 22.639 | 23.950 | 24.351 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.10 | 1.10–1.10 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
