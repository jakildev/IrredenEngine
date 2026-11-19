| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 120.270 | 120.270–120.270 | n/a |
| frame p95 | 201.790 | 201.790–201.790 | n/a |
| frame p99 | 239.350 | 239.350–239.350 | n/a |
| steady frame avg | 108.930 | 108.930–108.930 | n/a |
| steady frame p95 | 145.540 | 145.540–145.540 | n/a |
| steady frame p99 | 156.980 | 156.980–156.980 | n/a |
| frame split INPUT | 0.197 | 0.197–0.197 | n/a |
| frame split UPDATE | 77.813 | 77.813–77.813 | n/a |
| frame split RENDER | 40.852 | 40.852–40.852 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.413 | 1.413–1.413 | n/a |
| GPU exclusive stage sum | 1.985 | 1.985–1.985 | n/a |
| GPU bakeSunShadowMap | 0.141 | 0.141–0.141 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.010 | 0.010–0.010 | n/a |
| GPU cardinalSurfaceFb | 0.122 | 0.122–0.122 | n/a |
| GPU computeLightVolume | 0.033 | 0.033–0.033 | n/a |
| GPU computeSunShadow | 0.050 | 0.050–0.050 | n/a |
| GPU computeVoxelAO | 0.011 | 0.011–0.011 | n/a |
| GPU entityCanvasToFb | 1.311 | 1.311–1.311 | n/a |
| GPU fbToScreen | 0.021 | 0.021–0.021 | n/a |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCardinalElect | 0.024 | 0.024–0.024 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.078 | 0.078–0.078 | n/a |
| GPU voxelStage2 | 0.025 | 0.025–0.025 | n/a |
| GPU voxelSunFaces | 0.074 | 0.074–0.074 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 108.934 | 145.540 | 156.981 | 170.165 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.70 | 6.70–6.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
