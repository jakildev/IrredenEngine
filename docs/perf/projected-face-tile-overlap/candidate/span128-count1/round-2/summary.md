| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.520 | 9.520–9.520 |
| frame p95 | 9.660 | 9.660–9.660 |
| frame p99 | 37.800 | 37.800–37.800 |
| steady frame avg | 9.780 | 9.780–9.780 |
| steady frame p95 | 9.160 | 9.160–9.160 |
| steady frame p99 | 20.190 | 20.190–20.190 |
| GPU frame commandBufferSpans | 4.446 | 4.446–4.446 |
| GPU frame envelope | 5.487 | 5.487–5.487 |
| GPU canvasClear | 0.313 | 0.313–0.313 |
| GPU computeLightVolume | 1.307 | 1.307–1.307 |
| GPU computeSunShadow | 0.058 | 0.058–0.058 |
| GPU computeVoxelAO | 0.083 | 0.083–0.083 |
| GPU fbToScreen | 0.110 | 0.110–0.110 |
| GPU lightingToTrixel | 0.043 | 0.043–0.043 |
| GPU shapeCastBoxes | 0.123 | 0.123–0.123 |
| GPU shapeDepth | 0.708 | 0.708–0.708 |
| GPU shapeOwnerClear | 0.134 | 0.134–0.134 |
| GPU shapeOwnerElect | 1.046 | 1.046–1.046 |
| GPU shapePublish | 0.745 | 0.745–0.745 |
| GPU trixelToFb | 0.447 | 0.447–0.447 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.777 | 9.165 | 20.195 | 155.798 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
