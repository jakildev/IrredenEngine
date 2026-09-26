| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.560 | 9.560–9.560 |
| frame p95 | 9.350 | 9.350–9.350 |
| frame p99 | 46.240 | 46.240–46.240 |
| steady frame avg | 9.810 | 9.810–9.810 |
| steady frame p95 | 9.230 | 9.230–9.230 |
| steady frame p99 | 19.960 | 19.960–19.960 |
| GPU frame commandBufferSpans | 4.364 | 4.364–4.364 |
| GPU frame envelope | 5.437 | 5.437–5.437 |
| GPU canvasClear | 0.232 | 0.232–0.232 |
| GPU computeLightVolume | 0.804 | 0.804–0.804 |
| GPU computeSunShadow | 0.105 | 0.105–0.105 |
| GPU computeVoxelAO | 0.174 | 0.174–0.174 |
| GPU fbToScreen | 0.124 | 0.124–0.124 |
| GPU lightingToTrixel | 0.058 | 0.058–0.058 |
| GPU shapeCastBoxes | 0.292 | 0.292–0.292 |
| GPU shapeDepth | 0.693 | 0.693–0.693 |
| GPU shapeOwnerClear | 0.096 | 0.096–0.096 |
| GPU shapeOwnerElect | 0.778 | 0.778–0.778 |
| GPU shapePublish | 0.672 | 0.672–0.672 |
| GPU trixelToFb | 0.433 | 0.433–0.433 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.813 | 9.231 | 19.956 | 161.691 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
