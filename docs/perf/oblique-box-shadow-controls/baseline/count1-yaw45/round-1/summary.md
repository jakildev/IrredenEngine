| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.440 | 9.440–9.440 |
| frame p95 | 9.590 | 9.590–9.590 |
| frame p99 | 51.040 | 51.040–51.040 |
| steady frame avg | 9.580 | 9.580–9.580 |
| steady frame p95 | 9.590 | 9.590–9.590 |
| steady frame p99 | 17.590 | 17.590–17.590 |
| GPU frame commandBufferSpans | 4.281 | 4.281–4.281 |
| GPU frame envelope | 5.198 | 5.198–5.198 |
| GPU canvasClear | 0.049 | 0.049–0.049 |
| GPU computeLightVolume | 1.323 | 1.323–1.323 |
| GPU computeSunShadow | 0.552 | 0.552–0.552 |
| GPU computeVoxelAO | 0.926 | 0.926–0.926 |
| GPU fbToScreen | 0.161 | 0.161–0.161 |
| GPU lightingToTrixel | 0.185 | 0.185–0.185 |
| GPU shapeCastBoxes | 0.233 | 0.233–0.233 |
| GPU shapeDepth | 0.295 | 0.295–0.295 |
| GPU shapeOwnerClear | 0.025 | 0.025–0.025 |
| GPU shapeOwnerElect | 0.425 | 0.425–0.425 |
| GPU shapePublish | 0.912 | 0.912–0.912 |
| GPU trixelToFb | 0.114 | 0.114–0.114 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.580 | 9.588 | 17.592 | 141.054 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
