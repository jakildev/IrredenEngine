| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.530 | 9.530–9.530 |
| frame p95 | 8.380 | 8.380–8.380 |
| frame p99 | 86.410 | 86.410–86.410 |
| steady frame avg | 9.550 | 9.550–9.550 |
| steady frame p95 | 8.380 | 8.380–8.380 |
| steady frame p99 | 8.870 | 8.870–8.870 |
| GPU frame commandBufferSpans | 3.252 | 3.252–3.252 |
| GPU frame envelope | 4.131 | 4.131–4.131 |
| GPU canvasClear | 0.040 | 0.040–0.040 |
| GPU computeLightVolume | 0.469 | 0.469–0.469 |
| GPU computeSunShadow | 0.061 | 0.061–0.061 |
| GPU computeVoxelAO | 0.043 | 0.043–0.043 |
| GPU fbToScreen | 0.119 | 0.119–0.119 |
| GPU lightingToTrixel | 0.047 | 0.047–0.047 |
| GPU shapeCastBoxes | 0.486 | 0.486–0.486 |
| GPU shapeDepth | 0.507 | 0.507–0.507 |
| GPU shapeOwnerClear | 0.024 | 0.024–0.024 |
| GPU shapeOwnerElect | 0.504 | 0.504–0.504 |
| GPU shapePublish | 0.550 | 0.550–0.550 |
| GPU trixelToFb | 0.124 | 0.124–0.124 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.546 | 8.378 | 8.875 | 136.186 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
