| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 16.780 | 16.780–16.780 |
| frame p95 | 16.820 | 16.820–16.820 |
| frame p99 | 64.740 | 64.740–64.740 |
| steady frame avg | 16.800 | 16.800–16.800 |
| steady frame p95 | 16.760 | 16.760–16.760 |
| steady frame p99 | 30.910 | 30.910–30.910 |
| GPU frame commandBufferSpans | 15.197 | 15.197–15.197 |
| GPU frame envelope | 16.016 | 16.016–16.016 |
| GPU canvasClear | 0.022 | 0.022–0.022 |
| GPU computeLightVolume | 0.426 | 0.426–0.426 |
| GPU computeSunShadow | 0.043 | 0.043–0.043 |
| GPU computeVoxelAO | 0.040 | 0.040–0.040 |
| GPU fbToScreen | 0.037 | 0.037–0.037 |
| GPU lightingToTrixel | 0.044 | 0.044–0.044 |
| GPU shapeCastBoxes | 10.905 | 10.905–10.905 |
| GPU shapeDepth | 1.238 | 1.238–1.238 |
| GPU shapeOwnerClear | 0.009 | 0.009–0.009 |
| GPU shapeOwnerElect | 1.305 | 1.305–1.305 |
| GPU shapePublish | 1.318 | 1.318–1.318 |
| GPU trixelToFb | 0.147 | 0.147–0.147 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 16.799 | 16.760 | 30.914 | 131.527 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
