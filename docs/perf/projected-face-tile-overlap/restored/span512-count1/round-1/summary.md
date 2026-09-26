| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.630 | 9.630–9.630 |
| frame p95 | 9.700 | 9.700–9.700 |
| frame p99 | 47.860 | 47.860–47.860 |
| steady frame avg | 9.870 | 9.870–9.870 |
| steady frame p95 | 9.700 | 9.700–9.700 |
| steady frame p99 | 17.790 | 17.790–17.790 |
| GPU frame commandBufferSpans | 5.522 | 5.522–5.522 |
| GPU frame envelope | 6.595 | 6.595–6.595 |
| GPU canvasClear | 0.126 | 0.126–0.126 |
| GPU computeLightVolume | 0.581 | 0.581–0.581 |
| GPU computeSunShadow | 0.039 | 0.039–0.039 |
| GPU computeVoxelAO | 0.033 | 0.033–0.033 |
| GPU fbToScreen | 0.044 | 0.044–0.044 |
| GPU lightingToTrixel | 0.038 | 0.038–0.038 |
| GPU shapeCastBoxes | 0.434 | 0.434–0.434 |
| GPU shapeDepth | 1.728 | 1.728–1.728 |
| GPU shapeOwnerClear | 0.048 | 0.048–0.048 |
| GPU shapeOwnerElect | 1.317 | 1.317–1.317 |
| GPU shapePublish | 1.255 | 1.255–1.255 |
| GPU trixelToFb | 0.262 | 0.262–0.262 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.868 | 9.695 | 17.789 | 158.335 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
