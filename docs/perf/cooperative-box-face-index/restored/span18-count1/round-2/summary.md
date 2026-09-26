| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.570 | 9.570–9.570 |
| frame p95 | 10.040 | 10.040–10.040 |
| frame p99 | 49.390 | 49.390–49.390 |
| steady frame avg | 9.780 | 9.780–9.780 |
| steady frame p95 | 9.250 | 9.250–9.250 |
| steady frame p99 | 17.980 | 17.980–17.980 |
| GPU frame commandBufferSpans | 4.122 | 4.122–4.122 |
| GPU frame envelope | 5.217 | 5.217–5.217 |
| GPU canvasClear | 0.293 | 0.293–0.293 |
| GPU computeLightVolume | 0.440 | 0.440–0.440 |
| GPU computeSunShadow | 0.087 | 0.087–0.087 |
| GPU computeVoxelAO | 0.146 | 0.146–0.146 |
| GPU fbToScreen | 0.103 | 0.103–0.103 |
| GPU lightingToTrixel | 0.051 | 0.051–0.051 |
| GPU shapeCastBoxes | 1.316 | 1.316–1.316 |
| GPU shapeDepth | 0.323 | 0.323–0.323 |
| GPU shapeOwnerClear | 0.104 | 0.104–0.104 |
| GPU shapeOwnerElect | 0.271 | 0.271–0.271 |
| GPU shapePublish | 0.320 | 0.320–0.320 |
| GPU trixelToFb | 0.444 | 0.444–0.444 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.779 | 9.251 | 17.975 | 161.241 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
