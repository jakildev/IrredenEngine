| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.550 | 9.550–9.550 |
| frame p95 | 9.670 | 9.670–9.670 |
| frame p99 | 40.050 | 40.050–40.050 |
| steady frame avg | 9.840 | 9.840–9.840 |
| steady frame p95 | 9.480 | 9.480–9.480 |
| steady frame p99 | 27.600 | 27.600–27.600 |
| GPU frame commandBufferSpans | 3.945 | 3.945–3.945 |
| GPU frame envelope | 4.995 | 4.995–4.995 |
| GPU canvasClear | 0.220 | 0.220–0.220 |
| GPU computeLightVolume | 0.545 | 0.545–0.545 |
| GPU computeSunShadow | 0.046 | 0.046–0.046 |
| GPU computeVoxelAO | 0.050 | 0.050–0.050 |
| GPU fbToScreen | 0.085 | 0.085–0.085 |
| GPU lightingToTrixel | 0.033 | 0.033–0.033 |
| GPU shapeCastBoxes | 1.119 | 1.119–1.119 |
| GPU shapeDepth | 0.403 | 0.403–0.403 |
| GPU shapeOwnerClear | 0.066 | 0.066–0.066 |
| GPU shapeOwnerElect | 0.528 | 0.528–0.528 |
| GPU shapePublish | 0.477 | 0.477–0.477 |
| GPU trixelToFb | 0.393 | 0.393–0.393 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.840 | 9.476 | 27.598 | 155.050 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
