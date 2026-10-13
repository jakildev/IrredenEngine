| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.340 | 9.340–9.340 |
| frame p95 | 8.430 | 8.430–8.430 |
| frame p99 | 29.640 | 29.640–29.640 |
| steady frame avg | 9.570 | 9.570–9.570 |
| steady frame p95 | 8.390 | 8.390–8.390 |
| steady frame p99 | 9.380 | 9.380–9.380 |
| GPU frame commandBufferSpans | 3.157 | 3.157–3.157 |
| GPU frame envelope | 4.051 | 4.051–4.051 |
| GPU canvasClear | 0.041 | 0.041–0.041 |
| GPU computeLightVolume | 0.486 | 0.486–0.486 |
| GPU computeSunShadow | 0.063 | 0.063–0.063 |
| GPU computeVoxelAO | 0.044 | 0.044–0.044 |
| GPU fbToScreen | 0.121 | 0.121–0.121 |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 |
| GPU shapeCastBoxes | 0.522 | 0.522–0.522 |
| GPU shapeDepth | 0.522 | 0.522–0.522 |
| GPU shapeOwnerClear | 0.024 | 0.024–0.024 |
| GPU shapeOwnerElect | 0.519 | 0.519–0.519 |
| GPU shapePublish | 0.567 | 0.567–0.567 |
| GPU trixelToFb | 0.123 | 0.123–0.123 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.571 | 8.391 | 9.384 | 139.650 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
