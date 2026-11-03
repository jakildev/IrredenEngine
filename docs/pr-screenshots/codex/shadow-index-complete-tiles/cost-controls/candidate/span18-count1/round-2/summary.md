| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.330 | 9.330–9.330 |
| frame p95 | 8.870 | 8.870–8.870 |
| frame p99 | 30.400 | 30.400–30.400 |
| steady frame avg | 9.560 | 9.560–9.560 |
| steady frame p95 | 8.840 | 8.840–8.840 |
| steady frame p99 | 9.490 | 9.490–9.490 |
| GPU frame commandBufferSpans | 2.124 | 2.124–2.124 |
| GPU frame envelope | 3.012 | 3.012–3.012 |
| GPU canvasClear | 0.042 | 0.042–0.042 |
| GPU computeLightVolume | 0.298 | 0.298–0.298 |
| GPU computeSunShadow | 0.063 | 0.063–0.063 |
| GPU computeVoxelAO | 0.044 | 0.044–0.044 |
| GPU fbToScreen | 0.121 | 0.121–0.121 |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 |
| GPU shapeCastBoxes | 0.131 | 0.131–0.131 |
| GPU shapeDepth | 0.308 | 0.308–0.308 |
| GPU shapeOwnerClear | 0.025 | 0.025–0.025 |
| GPU shapeOwnerElect | 0.324 | 0.324–0.324 |
| GPU shapePublish | 0.326 | 0.326–0.326 |
| GPU trixelToFb | 0.128 | 0.128–0.128 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.556 | 8.839 | 9.492 | 137.143 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
