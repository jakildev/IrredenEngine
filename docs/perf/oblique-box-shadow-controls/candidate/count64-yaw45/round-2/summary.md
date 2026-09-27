| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.440 | 9.440–9.440 |
| frame p95 | 9.190 | 9.190–9.190 |
| frame p99 | 42.580 | 42.580–42.580 |
| steady frame avg | 9.640 | 9.640–9.640 |
| steady frame p95 | 8.840 | 8.840–8.840 |
| steady frame p99 | 19.290 | 19.290–19.290 |
| GPU frame commandBufferSpans | 3.656 | 3.656–3.656 |
| GPU frame envelope | 4.579 | 4.579–4.579 |
| GPU canvasClear | 0.026 | 0.026–0.026 |
| GPU computeLightVolume | 1.596 | 1.596–1.596 |
| GPU computeSunShadow | 0.103 | 0.103–0.103 |
| GPU computeVoxelAO | 0.145 | 0.145–0.145 |
| GPU fbToScreen | 0.098 | 0.098–0.098 |
| GPU lightingToTrixel | 0.055 | 0.055–0.055 |
| GPU shapeCastBoxes | 0.272 | 0.272–0.272 |
| GPU shapeDepth | 0.430 | 0.430–0.430 |
| GPU shapeOwnerClear | 0.016 | 0.016–0.016 |
| GPU shapeOwnerElect | 1.070 | 1.070–1.070 |
| GPU shapePublish | 1.060 | 1.060–1.060 |
| GPU trixelToFb | 0.075 | 0.075–0.075 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.642 | 8.836 | 19.288 | 139.564 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
