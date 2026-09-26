| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.700 | 9.700–9.700 |
| frame p95 | 9.550 | 9.550–9.550 |
| frame p99 | 106.350 | 106.350–106.350 |
| steady frame avg | 9.570 | 9.570–9.570 |
| steady frame p95 | 8.600 | 8.600–8.600 |
| steady frame p99 | 11.220 | 11.220–11.220 |
| GPU frame commandBufferSpans | 4.121 | 4.121–4.121 |
| GPU frame envelope | 5.104 | 5.104–5.104 |
| GPU canvasClear | 0.044 | 0.044–0.044 |
| GPU computeLightVolume | 1.971 | 1.971–1.971 |
| GPU computeSunShadow | 0.341 | 0.341–0.341 |
| GPU computeVoxelAO | 0.607 | 0.607–0.607 |
| GPU fbToScreen | 0.134 | 0.134–0.134 |
| GPU lightingToTrixel | 0.164 | 0.164–0.164 |
| GPU shapeCastBoxes | 0.230 | 0.230–0.230 |
| GPU shapeDepth | 0.279 | 0.279–0.279 |
| GPU shapeOwnerClear | 0.022 | 0.022–0.022 |
| GPU shapeOwnerElect | 0.531 | 0.531–0.531 |
| GPU shapePublish | 1.249 | 1.249–1.249 |
| GPU trixelToFb | 0.107 | 0.107–0.107 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.567 | 8.603 | 11.217 | 154.371 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
