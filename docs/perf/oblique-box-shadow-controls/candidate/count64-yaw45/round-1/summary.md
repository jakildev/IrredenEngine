| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.570 | 9.570–9.570 |
| frame p95 | 8.930 | 8.930–8.930 |
| frame p99 | 74.840 | 74.840–74.840 |
| steady frame avg | 9.680 | 9.680–9.680 |
| steady frame p95 | 8.780 | 8.780–8.780 |
| steady frame p99 | 19.360 | 19.360–19.360 |
| GPU frame commandBufferSpans | 3.992 | 3.992–3.992 |
| GPU frame envelope | 4.975 | 4.975–4.975 |
| GPU canvasClear | 0.033 | 0.033–0.033 |
| GPU computeLightVolume | 1.702 | 1.702–1.702 |
| GPU computeSunShadow | 0.113 | 0.113–0.113 |
| GPU computeVoxelAO | 0.157 | 0.157–0.157 |
| GPU fbToScreen | 0.110 | 0.110–0.110 |
| GPU lightingToTrixel | 0.061 | 0.061–0.061 |
| GPU shapeCastBoxes | 0.289 | 0.289–0.289 |
| GPU shapeDepth | 0.462 | 0.462–0.462 |
| GPU shapeOwnerClear | 0.018 | 0.018–0.018 |
| GPU shapeOwnerElect | 1.408 | 1.408–1.408 |
| GPU shapePublish | 0.894 | 0.894–0.894 |
| GPU trixelToFb | 0.083 | 0.083–0.083 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.683 | 8.780 | 19.356 | 147.812 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
