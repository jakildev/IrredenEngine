| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.520 | 9.520–9.520 |
| frame p95 | 9.390 | 9.390–9.390 |
| frame p99 | 44.070 | 44.070–44.070 |
| steady frame avg | 9.780 | 9.780–9.780 |
| steady frame p95 | 9.190 | 9.190–9.190 |
| steady frame p99 | 18.340 | 18.340–18.340 |
| GPU frame commandBufferSpans | 4.545 | 4.545–4.545 |
| GPU frame envelope | 5.620 | 5.620–5.620 |
| GPU canvasClear | 0.299 | 0.299–0.299 |
| GPU computeLightVolume | 1.502 | 1.502–1.502 |
| GPU computeSunShadow | 0.068 | 0.068–0.068 |
| GPU computeVoxelAO | 0.108 | 0.108–0.108 |
| GPU fbToScreen | 0.109 | 0.109–0.109 |
| GPU lightingToTrixel | 0.049 | 0.049–0.049 |
| GPU shapeCastBoxes | 0.127 | 0.127–0.127 |
| GPU shapeDepth | 0.757 | 0.757–0.757 |
| GPU shapeOwnerClear | 0.121 | 0.121–0.121 |
| GPU shapeOwnerElect | 1.069 | 1.069–1.069 |
| GPU shapePublish | 0.903 | 0.903–0.903 |
| GPU trixelToFb | 0.400 | 0.400–0.400 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.783 | 9.187 | 18.344 | 160.141 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
