| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.530 | 9.530–9.530 |
| frame p95 | 9.550 | 9.550–9.550 |
| frame p99 | 49.240 | 49.240–49.240 |
| steady frame avg | 9.770 | 9.770–9.770 |
| steady frame p95 | 9.580 | 9.580–9.580 |
| steady frame p99 | 19.210 | 19.210–19.210 |
| GPU frame commandBufferSpans | 2.869 | 2.869–2.869 |
| GPU frame envelope | 3.921 | 3.921–3.921 |
| GPU canvasClear | 0.137 | 0.137–0.137 |
| GPU computeLightVolume | 0.484 | 0.484–0.484 |
| GPU computeSunShadow | 0.060 | 0.060–0.060 |
| GPU computeVoxelAO | 0.063 | 0.063–0.063 |
| GPU fbToScreen | 0.090 | 0.090–0.090 |
| GPU lightingToTrixel | 0.042 | 0.042–0.042 |
| GPU shapeCastBoxes | 0.065 | 0.065–0.065 |
| GPU shapeDepth | 0.377 | 0.377–0.377 |
| GPU shapeOwnerClear | 0.058 | 0.058–0.058 |
| GPU shapeOwnerElect | 0.477 | 0.477–0.477 |
| GPU shapePublish | 0.301 | 0.301–0.301 |
| GPU trixelToFb | 0.393 | 0.393–0.393 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.773 | 9.585 | 19.214 | 156.542 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
