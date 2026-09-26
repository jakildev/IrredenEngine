| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.300 | 9.300–9.300 |
| frame p95 | 11.080 | 11.080–11.080 |
| frame p99 | 42.390 | 42.390–42.390 |
| steady frame avg | 9.500 | 9.500–9.500 |
| steady frame p95 | 11.120 | 11.120–11.120 |
| steady frame p99 | 19.640 | 19.640–19.640 |
| GPU frame commandBufferSpans | 4.167 | 4.167–4.167 |
| GPU frame envelope | 5.043 | 5.043–5.043 |
| GPU canvasClear | 0.063 | 0.063–0.063 |
| GPU computeLightVolume | 1.738 | 1.738–1.738 |
| GPU computeSunShadow | 0.297 | 0.297–0.297 |
| GPU computeVoxelAO | 0.601 | 0.601–0.601 |
| GPU fbToScreen | 0.163 | 0.163–0.163 |
| GPU lightingToTrixel | 0.118 | 0.118–0.118 |
| GPU shapeCastBoxes | 0.261 | 0.261–0.261 |
| GPU shapeDepth | 0.342 | 0.342–0.342 |
| GPU shapeOwnerClear | 0.031 | 0.031–0.031 |
| GPU shapeOwnerElect | 0.599 | 0.599–0.599 |
| GPU shapePublish | 1.198 | 1.198–1.198 |
| GPU trixelToFb | 0.124 | 0.124–0.124 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.499 | 11.121 | 19.643 | 136.147 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
