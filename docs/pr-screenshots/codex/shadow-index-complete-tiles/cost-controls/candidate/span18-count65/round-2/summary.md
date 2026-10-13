| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.340 | 9.340–9.340 |
| frame p95 | 8.720 | 8.720–8.720 |
| frame p99 | 28.770 | 28.770–28.770 |
| steady frame avg | 9.580 | 9.580–9.580 |
| steady frame p95 | 8.720 | 8.720–8.720 |
| steady frame p99 | 9.400 | 9.400–9.400 |
| GPU frame commandBufferSpans | 3.159 | 3.159–3.159 |
| GPU frame envelope | 4.057 | 4.057–4.057 |
| GPU canvasClear | 0.041 | 0.041–0.041 |
| GPU computeLightVolume | 0.486 | 0.486–0.486 |
| GPU computeSunShadow | 0.063 | 0.063–0.063 |
| GPU computeVoxelAO | 0.044 | 0.044–0.044 |
| GPU fbToScreen | 0.121 | 0.121–0.121 |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 |
| GPU shapeCastBoxes | 0.524 | 0.524–0.524 |
| GPU shapeDepth | 0.522 | 0.522–0.522 |
| GPU shapeOwnerClear | 0.024 | 0.024–0.024 |
| GPU shapeOwnerElect | 0.521 | 0.521–0.521 |
| GPU shapePublish | 0.568 | 0.568–0.568 |
| GPU trixelToFb | 0.124 | 0.124–0.124 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.577 | 8.716 | 9.403 | 138.182 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
