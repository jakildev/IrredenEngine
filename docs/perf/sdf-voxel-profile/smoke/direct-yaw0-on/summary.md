| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 8.920 | 8.920–8.920 |
| frame p95 | 9.260 | 9.260–9.260 |
| frame p99 | 17.960 | 17.960–17.960 |
| steady frame avg | 8.570 | 8.570–8.570 |
| steady frame p95 | 9.190 | 9.190–9.190 |
| steady frame p99 | 16.990 | 16.990–16.990 |
| GPU frame commandBufferSpans | 3.901 | 3.901–3.901 |
| GPU frame envelope | 3.901 | 3.901–3.901 |
| GPU canvasClear | 0.077 | 0.077–0.077 |
| GPU computeLightVolume | 2.454 | 2.454–2.454 |
| GPU computeSunShadow | 0.250 | 0.250–0.250 |
| GPU computeVoxelAO | 0.032 | 0.032–0.032 |
| GPU fbToScreen | 0.122 | 0.122–0.122 |
| GPU lightingToTrixel | 0.063 | 0.063–0.063 |
| GPU shapeCastBake | 1.172 | 1.172–1.172 |
| GPU shapeCastBoxes | 1.105 | 1.105–1.105 |
| GPU shapeCastClear | 0.032 | 0.032–0.032 |
| GPU shapeCastFallback | 0.030 | 0.030–0.030 |
| GPU shapeCastResolve | 0.040 | 0.040–0.040 |
| GPU shapeDepth | 0.154 | 0.154–0.154 |
| GPU shapeOwnerClear | 0.016 | 0.016–0.016 |
| GPU shapeOwnerElect | 0.153 | 0.153–0.153 |
| GPU shapePublish | 0.160 | 0.160–0.160 |
| GPU trixelToFb | 0.169 | 0.169–0.169 |
| GPU voxelCardinalElect | 0.101 | 0.101–0.101 |
| GPU voxelCompact | 0.053 | 0.053–0.053 |
| GPU voxelStage1 | 0.194 | 0.194–0.194 |
| GPU voxelStage2 | 0.113 | 0.113–0.113 |
| GPU voxelSunFaces | 0.121 | 0.121–0.121 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 8.571 | 9.192 | 16.989 | 18.035 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
