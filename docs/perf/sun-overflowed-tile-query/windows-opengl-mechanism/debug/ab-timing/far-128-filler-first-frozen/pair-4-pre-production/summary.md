| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 137.580 | 137.580–137.580 | n/a |
| frame p95 | 221.600 | 221.600–221.600 | n/a |
| frame p99 | 272.440 | 272.440–272.440 | n/a |
| steady frame avg | 128.910 | 128.910–128.910 | n/a |
| steady frame p95 | 162.420 | 162.420–162.420 | n/a |
| steady frame p99 | 183.270 | 183.270–183.270 | n/a |
| frame split INPUT | 0.243 | 0.243–0.243 | n/a |
| frame split UPDATE | 89.921 | 89.921–89.921 | n/a |
| frame split RENDER | 43.353 | 43.353–43.353 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 4.067 | 4.067–4.067 | n/a |
| GPU exclusive stage sum | 2.146 | 2.146–2.146 | n/a |
| GPU bakeSunShadowMap | 0.156 | 0.156–0.156 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.011 | 0.011–0.011 | n/a |
| GPU cardinalSurfaceFb | 0.135 | 0.135–0.135 | n/a |
| GPU computeLightVolume | 0.036 | 0.036–0.036 | n/a |
| GPU computeSunShadow | 0.054 | 0.054–0.054 | n/a |
| GPU computeVoxelAO | 0.011 | 0.011–0.011 | n/a |
| GPU entityCanvasToFb | 1.399 | 1.399–1.399 | n/a |
| GPU fbToScreen | 0.023 | 0.023–0.023 | n/a |
| GPU lightingToTrixel | 0.052 | 0.052–0.052 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCardinalElect | 0.026 | 0.026–0.026 | n/a |
| GPU voxelCompact | 0.014 | 0.014–0.014 | n/a |
| GPU voxelStage1 | 0.092 | 0.092–0.092 | n/a |
| GPU voxelStage2 | 0.029 | 0.029–0.029 | n/a |
| GPU voxelSunFaces | 0.081 | 0.081–0.081 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 128.911 | 162.423 | 183.272 | 194.757 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 7.30 | 7.30–7.30 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
