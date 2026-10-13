| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.330 | 9.330–9.330 |
| frame p95 | 8.420 | 8.420–8.420 |
| frame p99 | 32.190 | 32.190–32.190 |
| steady frame avg | 9.550 | 9.550–9.550 |
| steady frame p95 | 8.400 | 8.400–8.400 |
| steady frame p99 | 8.830 | 8.830–8.830 |
| GPU frame commandBufferSpans | 2.942 | 2.942–2.942 |
| GPU frame envelope | 3.819 | 3.819–3.819 |
| GPU canvasClear | 0.040 | 0.040–0.040 |
| GPU computeLightVolume | 0.544 | 0.544–0.544 |
| GPU computeSunShadow | 0.067 | 0.067–0.067 |
| GPU computeVoxelAO | 0.045 | 0.045–0.045 |
| GPU fbToScreen | 0.118 | 0.118–0.118 |
| GPU lightingToTrixel | 0.051 | 0.051–0.051 |
| GPU shapeCastBoxes | 0.139 | 0.139–0.139 |
| GPU shapeDepth | 0.592 | 0.592–0.592 |
| GPU shapeOwnerClear | 0.024 | 0.024–0.024 |
| GPU shapeOwnerElect | 0.579 | 0.579–0.579 |
| GPU shapePublish | 0.617 | 0.617–0.617 |
| GPU trixelToFb | 0.121 | 0.121–0.121 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.546 | 8.396 | 8.834 | 134.875 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
