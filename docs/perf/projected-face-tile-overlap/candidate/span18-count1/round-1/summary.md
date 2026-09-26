| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.580 | 9.580–9.580 |
| frame p95 | 9.920 | 9.920–9.920 |
| frame p99 | 71.230 | 71.230–71.230 |
| steady frame avg | 9.740 | 9.740–9.740 |
| steady frame p95 | 8.970 | 8.970–8.970 |
| steady frame p99 | 18.870 | 18.870–18.870 |
| GPU frame commandBufferSpans | 4.591 | 4.591–4.591 |
| GPU frame envelope | 5.663 | 5.663–5.663 |
| GPU canvasClear | 0.416 | 0.416–0.416 |
| GPU computeLightVolume | 1.535 | 1.535–1.535 |
| GPU computeSunShadow | 0.134 | 0.134–0.134 |
| GPU computeVoxelAO | 0.247 | 0.247–0.247 |
| GPU fbToScreen | 0.137 | 0.137–0.137 |
| GPU lightingToTrixel | 0.081 | 0.081–0.081 |
| GPU shapeCastBoxes | 0.188 | 0.188–0.188 |
| GPU shapeDepth | 0.507 | 0.507–0.507 |
| GPU shapeOwnerClear | 0.169 | 0.169–0.169 |
| GPU shapeOwnerElect | 0.605 | 0.605–0.605 |
| GPU shapePublish | 1.038 | 1.038–1.038 |
| GPU trixelToFb | 0.419 | 0.419–0.419 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.736 | 8.965 | 18.875 | 157.167 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
