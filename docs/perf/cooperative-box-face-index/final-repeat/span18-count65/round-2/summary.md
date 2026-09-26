| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.560 | 9.560–9.560 |
| frame p95 | 9.670 | 9.670–9.670 |
| frame p99 | 47.050 | 47.050–47.050 |
| steady frame avg | 9.770 | 9.770–9.770 |
| steady frame p95 | 9.470 | 9.470–9.470 |
| steady frame p99 | 18.580 | 18.580–18.580 |
| GPU frame commandBufferSpans | 4.278 | 4.278–4.278 |
| GPU frame envelope | 5.344 | 5.344–5.344 |
| GPU canvasClear | 0.282 | 0.282–0.282 |
| GPU computeLightVolume | 1.088 | 1.088–1.088 |
| GPU computeSunShadow | 0.047 | 0.047–0.047 |
| GPU computeVoxelAO | 0.070 | 0.070–0.070 |
| GPU fbToScreen | 0.105 | 0.105–0.105 |
| GPU lightingToTrixel | 0.037 | 0.037–0.037 |
| GPU shapeCastBoxes | 0.268 | 0.268–0.268 |
| GPU shapeDepth | 0.652 | 0.652–0.652 |
| GPU shapeOwnerClear | 0.096 | 0.096–0.096 |
| GPU shapeOwnerElect | 1.048 | 1.048–1.048 |
| GPU shapePublish | 0.579 | 0.579–0.579 |
| GPU trixelToFb | 0.413 | 0.413–0.413 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.767 | 9.466 | 18.579 | 155.079 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
