| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.450 | 9.450–9.450 |
| frame p95 | 8.860 | 8.860–8.860 |
| frame p99 | 58.020 | 58.020–58.020 |
| steady frame avg | 9.620 | 9.620–9.620 |
| steady frame p95 | 8.760 | 8.760–8.760 |
| steady frame p99 | 19.400 | 19.400–19.400 |
| GPU frame commandBufferSpans | 5.199 | 5.199–5.199 |
| GPU frame envelope | 6.147 | 6.147–6.147 |
| GPU canvasClear | 0.039 | 0.039–0.039 |
| GPU computeLightVolume | 2.538 | 2.538–2.538 |
| GPU computeSunShadow | 0.087 | 0.087–0.087 |
| GPU computeVoxelAO | 0.098 | 0.098–0.098 |
| GPU fbToScreen | 0.126 | 0.126–0.126 |
| GPU lightingToTrixel | 0.054 | 0.054–0.054 |
| GPU shapeCastBoxes | 0.347 | 0.347–0.347 |
| GPU shapeDepth | 0.651 | 0.651–0.651 |
| GPU shapeOwnerClear | 0.022 | 0.022–0.022 |
| GPU shapeOwnerElect | 1.956 | 1.956–1.956 |
| GPU shapePublish | 1.373 | 1.373–1.373 |
| GPU trixelToFb | 0.113 | 0.113–0.113 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.623 | 8.757 | 19.404 | 142.173 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
