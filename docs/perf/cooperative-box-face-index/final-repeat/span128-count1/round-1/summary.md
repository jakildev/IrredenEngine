| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.530 | 9.530–9.530 |
| frame p95 | 9.630 | 9.630–9.630 |
| frame p99 | 43.890 | 43.890–43.890 |
| steady frame avg | 9.750 | 9.750–9.750 |
| steady frame p95 | 9.590 | 9.590–9.590 |
| steady frame p99 | 17.980 | 17.980–17.980 |
| GPU frame commandBufferSpans | 4.369 | 4.369–4.369 |
| GPU frame envelope | 5.417 | 5.417–5.417 |
| GPU canvasClear | 0.270 | 0.270–0.270 |
| GPU computeLightVolume | 1.285 | 1.285–1.285 |
| GPU computeSunShadow | 0.052 | 0.052–0.052 |
| GPU computeVoxelAO | 0.090 | 0.090–0.090 |
| GPU fbToScreen | 0.102 | 0.102–0.102 |
| GPU lightingToTrixel | 0.041 | 0.041–0.041 |
| GPU shapeCastBoxes | 0.109 | 0.109–0.109 |
| GPU shapeDepth | 0.669 | 0.669–0.669 |
| GPU shapeOwnerClear | 0.104 | 0.104–0.104 |
| GPU shapeOwnerElect | 1.035 | 1.035–1.035 |
| GPU shapePublish | 0.714 | 0.714–0.714 |
| GPU trixelToFb | 0.461 | 0.461–0.461 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.749 | 9.590 | 17.976 | 152.533 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
