| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.510 | 9.510–9.510 |
| frame p95 | 9.480 | 9.480–9.480 |
| frame p99 | 37.490 | 37.490–37.490 |
| steady frame avg | 9.800 | 9.800–9.800 |
| steady frame p95 | 9.440 | 9.440–9.440 |
| steady frame p99 | 20.400 | 20.400–20.400 |
| GPU frame commandBufferSpans | 4.712 | 4.712–4.712 |
| GPU frame envelope | 5.780 | 5.780–5.780 |
| GPU canvasClear | 0.620 | 0.620–0.620 |
| GPU computeLightVolume | 0.935 | 0.935–0.935 |
| GPU computeSunShadow | 0.181 | 0.181–0.181 |
| GPU computeVoxelAO | 0.281 | 0.281–0.281 |
| GPU fbToScreen | 0.161 | 0.161–0.161 |
| GPU lightingToTrixel | 0.088 | 0.088–0.088 |
| GPU shapeCastBoxes | 0.162 | 0.162–0.162 |
| GPU shapeDepth | 0.553 | 0.553–0.553 |
| GPU shapeOwnerClear | 0.223 | 0.223–0.223 |
| GPU shapeOwnerElect | 0.489 | 0.489–0.489 |
| GPU shapePublish | 0.631 | 0.631–0.631 |
| GPU trixelToFb | 0.470 | 0.470–0.470 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.801 | 9.440 | 20.402 | 156.586 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
