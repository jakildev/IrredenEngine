| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 48.060 | 48.060–48.060 | n/a |
| frame p95 | 111.540 | 111.540–111.540 | n/a |
| frame p99 | 133.690 | 133.690–133.690 | n/a |
| steady frame avg | 41.140 | 41.140–41.140 | n/a |
| steady frame p95 | 44.630 | 44.630–44.630 | n/a |
| steady frame p99 | 50.870 | 50.870–50.870 | n/a |
| frame split INPUT | 0.111 | 0.111–0.111 | n/a |
| frame split UPDATE | 3.534 | 3.534–3.534 | n/a |
| frame split RENDER | 39.086 | 39.086–39.086 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 5.328 | 5.328–5.328 | n/a |
| GPU exclusive stage sum | 3.840 | 3.840–3.840 | n/a |
| GPU bakeSunShadowMap | 0.108 | 0.108–0.108 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.006 | 0.006–0.006 | n/a |
| GPU cardinalSurfaceFb | 0.126 | 0.126–0.126 | n/a |
| GPU computeLightVolume | 0.029 | 0.029–0.029 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.011 | 0.011–0.011 | n/a |
| GPU entityCanvasToFb | 3.356 | 3.356–3.356 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.027 | 0.027–0.027 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.014 | 0.014–0.014 | n/a |
| GPU voxelStage1 | 0.023 | 0.023–0.023 | n/a |
| GPU voxelStage2 | 0.012 | 0.012–0.012 | n/a |
| GPU voxelSunFaces | 0.069 | 0.069–0.069 | n/a |
| input_to_present mean | 78.745 | 78.745–78.745 | n/a |
| input_to_present p99 | 86.084 | 86.084–86.084 | n/a |
| cursor_to_present mean | 77.494 | 77.494–77.494 | n/a |
| cursor_to_present p99 | 84.548 | 84.548–84.548 | n/a |
| frame_start_to_submit mean | 35.818 | 35.818–35.818 | n/a |
| frame_start_to_submit p99 | 50.828 | 50.828–50.828 | n/a |
| submit_to_present mean | 45.011 | 45.011–45.011 | n/a |
| submit_to_present p99 | 55.710 | 55.710–55.710 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 41.138 | 44.631 | 50.868 | 51.365 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.90 | 2.90–2.90 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.1009 |

Render device counters: unsupported by this backend.
