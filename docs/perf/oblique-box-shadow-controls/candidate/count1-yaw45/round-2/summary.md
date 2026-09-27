| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.930 | 9.930–9.930 |
| frame p95 | 8.850 | 8.850–8.850 |
| frame p99 | 118.410 | 118.410–118.410 |
| steady frame avg | 10.090 | 10.090–10.090 |
| steady frame p95 | 8.550 | 8.550–8.550 |
| steady frame p99 | 118.410 | 118.410–118.410 |
| GPU frame commandBufferSpans | 4.609 | 4.609–4.609 |
| GPU frame envelope | 5.527 | 5.527–5.527 |
| GPU canvasClear | 0.044 | 0.044–0.044 |
| GPU computeLightVolume | 1.442 | 1.442–1.442 |
| GPU computeSunShadow | 0.673 | 0.673–0.673 |
| GPU computeVoxelAO | 1.005 | 1.005–1.005 |
| GPU fbToScreen | 0.199 | 0.199–0.199 |
| GPU lightingToTrixel | 0.214 | 0.214–0.214 |
| GPU shapeCastBoxes | 0.271 | 0.271–0.271 |
| GPU shapeDepth | 0.314 | 0.314–0.314 |
| GPU shapeOwnerClear | 0.025 | 0.025–0.025 |
| GPU shapeOwnerElect | 0.432 | 0.432–0.432 |
| GPU shapePublish | 0.883 | 0.883–0.883 |
| GPU trixelToFb | 0.122 | 0.122–0.122 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 10.092 | 8.548 | 118.411 | 138.447 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
