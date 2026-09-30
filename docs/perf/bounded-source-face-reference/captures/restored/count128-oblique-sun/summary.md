| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.800 | 21.800–21.800 |
| frame p95 | 22.560 | 22.560–22.560 |
| frame p99 | 162.900 | 162.900–162.900 |
| steady frame avg | 21.580 | 21.580–21.580 |
| steady frame p95 | 20.480 | 20.480–20.480 |
| steady frame p99 | 164.110 | 164.110–164.110 |
| GPU frame commandBufferSpans | 18.327 | 18.327–18.327 |
| GPU frame envelope | 20.625 | 20.625–20.625 |
| GPU canvasClear | 0.013 | 0.013–0.013 |
| GPU computeLightVolume | 0.196 | 0.196–0.196 |
| GPU computeSunShadow | 0.044 | 0.044–0.044 |
| GPU computeVoxelAO | 0.034 | 0.034–0.034 |
| GPU fbToScreen | 0.042 | 0.042–0.042 |
| GPU lightingToTrixel | 0.036 | 0.036–0.036 |
| GPU shapeCastBoxes | 1.593 | 1.593–1.593 |
| GPU shapeDepth | 5.498 | 5.498–5.498 |
| GPU shapeOwnerClear | 0.007 | 0.007–0.007 |
| GPU shapeOwnerElect | 5.526 | 5.526–5.526 |
| GPU shapePublish | 5.233 | 5.233–5.233 |
| GPU trixelToFb | 0.285 | 0.285–0.285 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 63 of each run excluded | 192 | 21.577 | 20.483 | 164.114 | 173.276 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 255 / 255 | 0 | 263 | 0.000 (270.000) | 0 / 0 (0) |
