| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.440 | 9.440–9.440 |
| frame p95 | 9.080 | 9.080–9.080 |
| frame p99 | 43.880 | 43.880–43.880 |
| steady frame avg | 9.660 | 9.660–9.660 |
| steady frame p95 | 8.960 | 8.960–8.960 |
| steady frame p99 | 18.580 | 18.580–18.580 |
| GPU frame commandBufferSpans | 4.773 | 4.773–4.773 |
| GPU frame envelope | 5.750 | 5.750–5.750 |
| GPU canvasClear | 0.339 | 0.339–0.339 |
| GPU computeLightVolume | 1.340 | 1.340–1.340 |
| GPU computeSunShadow | 0.068 | 0.068–0.068 |
| GPU computeVoxelAO | 0.069 | 0.069–0.069 |
| GPU fbToScreen | 0.108 | 0.108–0.108 |
| GPU lightingToTrixel | 0.044 | 0.044–0.044 |
| GPU shapeCastBoxes | 0.309 | 0.309–0.309 |
| GPU shapeDepth | 0.779 | 0.779–0.779 |
| GPU shapeOwnerClear | 0.082 | 0.082–0.082 |
| GPU shapeOwnerElect | 1.339 | 1.339–1.339 |
| GPU shapePublish | 0.644 | 0.644–0.644 |
| GPU trixelToFb | 0.336 | 0.336–0.336 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.660 | 8.960 | 18.584 | 152.561 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
