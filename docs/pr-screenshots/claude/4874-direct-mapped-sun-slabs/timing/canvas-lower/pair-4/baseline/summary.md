| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 9.450 | 9.450–9.450 | n/a |
| frame p95 | 17.450 | 17.450–17.450 | n/a |
| frame p99 | 18.680 | 18.680–18.680 | n/a |
| steady frame avg | 9.130 | 9.130–9.130 | n/a |
| steady frame p95 | 17.170 | 17.170–17.170 | n/a |
| steady frame p99 | 18.110 | 18.110–18.110 | n/a |
| GPU frame commandBufferSpans | 2.382 | 2.382–2.382 | n/a |
| GPU frame envelope | 2.382 | 2.382–2.382 | n/a |
| GPU bakeSunShadowMap | 1.362 | 1.362–1.362 | n/a |
| GPU canvasClear | 0.036 | 0.036–0.036 | n/a |
| GPU cardinalSurfaceFb | 1.775 | 1.775–1.775 | n/a |
| GPU computeLightVolume | 0.271 | 0.271–0.271 | n/a |
| GPU computeSunShadow | 0.056 | 0.056–0.056 | n/a |
| GPU computeVoxelAO | 0.089 | 0.089–0.089 | n/a |
| GPU fbToScreen | 0.117 | 0.117–0.117 | n/a |
| GPU lightingToTrixel | 0.072 | 0.072–0.072 | n/a |
| GPU trixelToFb | 1.529 | 1.529–1.529 | n/a |
| GPU voxelCardinalElect | 0.188 | 0.188–0.188 | n/a |
| GPU voxelCompact | 0.110 | 0.110–0.110 | n/a |
| GPU voxelStage1 | 0.105 | 0.105–0.105 | n/a |
| GPU voxelStage2 | 0.197 | 0.197–0.197 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 9.125 | 17.166 | 18.110 | 18.675 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.60 | 0.60–0.60 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
