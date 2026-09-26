| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.520 | 9.520–9.520 |
| frame p95 | 9.270 | 9.270–9.270 |
| frame p99 | 43.730 | 43.730–43.730 |
| steady frame avg | 9.790 | 9.790–9.790 |
| steady frame p95 | 9.240 | 9.240–9.240 |
| steady frame p99 | 19.920 | 19.920–19.920 |
| GPU frame commandBufferSpans | 4.910 | 4.910–4.910 |
| GPU frame envelope | 5.973 | 5.973–5.973 |
| GPU canvasClear | 0.328 | 0.328–0.328 |
| GPU computeLightVolume | 1.263 | 1.263–1.263 |
| GPU computeSunShadow | 0.053 | 0.053–0.053 |
| GPU computeVoxelAO | 0.081 | 0.081–0.081 |
| GPU fbToScreen | 0.113 | 0.113–0.113 |
| GPU lightingToTrixel | 0.044 | 0.044–0.044 |
| GPU shapeCastBoxes | 0.303 | 0.303–0.303 |
| GPU shapeDepth | 0.714 | 0.714–0.714 |
| GPU shapeOwnerClear | 0.097 | 0.097–0.097 |
| GPU shapeOwnerElect | 1.256 | 1.256–1.256 |
| GPU shapePublish | 0.679 | 0.679–0.679 |
| GPU trixelToFb | 0.451 | 0.451–0.451 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.786 | 9.242 | 19.918 | 155.191 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
