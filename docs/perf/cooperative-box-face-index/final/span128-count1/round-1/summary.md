| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.540 | 9.540–9.540 |
| frame p95 | 9.900 | 9.900–9.900 |
| frame p99 | 48.320 | 48.320–48.320 |
| steady frame avg | 9.780 | 9.780–9.780 |
| steady frame p95 | 10.120 | 10.120–10.120 |
| steady frame p99 | 20.510 | 20.510–20.510 |
| GPU frame commandBufferSpans | 4.912 | 4.912–4.912 |
| GPU frame envelope | 5.954 | 5.954–5.954 |
| GPU canvasClear | 0.331 | 0.331–0.331 |
| GPU computeLightVolume | 1.679 | 1.679–1.679 |
| GPU computeSunShadow | 0.067 | 0.067–0.067 |
| GPU computeVoxelAO | 0.107 | 0.107–0.107 |
| GPU fbToScreen | 0.113 | 0.113–0.113 |
| GPU lightingToTrixel | 0.049 | 0.049–0.049 |
| GPU shapeCastBoxes | 0.122 | 0.122–0.122 |
| GPU shapeDepth | 0.813 | 0.813–0.813 |
| GPU shapeOwnerClear | 0.118 | 0.118–0.118 |
| GPU shapeOwnerElect | 1.318 | 1.318–1.318 |
| GPU shapePublish | 0.861 | 0.861–0.861 |
| GPU trixelToFb | 0.380 | 0.380–0.380 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.779 | 10.119 | 20.514 | 153.489 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
