| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.460 | 9.460–9.460 |
| frame p95 | 8.930 | 8.930–8.930 |
| frame p99 | 55.940 | 55.940–55.940 |
| steady frame avg | 9.640 | 9.640–9.640 |
| steady frame p95 | 8.780 | 8.780–8.780 |
| steady frame p99 | 17.550 | 17.550–17.550 |
| GPU frame commandBufferSpans | 4.329 | 4.329–4.329 |
| GPU frame envelope | 5.272 | 5.272–5.272 |
| GPU canvasClear | 0.026 | 0.026–0.026 |
| GPU computeLightVolume | 1.440 | 1.440–1.440 |
| GPU computeSunShadow | 0.035 | 0.035–0.035 |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 |
| GPU fbToScreen | 0.067 | 0.067–0.067 |
| GPU lightingToTrixel | 0.028 | 0.028–0.028 |
| GPU shapeCastBoxes | 0.514 | 0.514–0.514 |
| GPU shapeDepth | 0.729 | 0.729–0.729 |
| GPU shapeOwnerClear | 0.016 | 0.016–0.016 |
| GPU shapeOwnerElect | 1.623 | 1.623–1.623 |
| GPU shapePublish | 0.919 | 0.919–0.919 |
| GPU trixelToFb | 0.091 | 0.091–0.091 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.639 | 8.775 | 17.547 | 144.000 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
