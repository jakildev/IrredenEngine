| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 46.340 | 46.340–46.340 | n/a |
| frame p95 | 113.760 | 113.760–113.760 | n/a |
| frame p99 | 150.710 | 150.710–150.710 | n/a |
| steady frame avg | 39.210 | 39.210–39.210 | n/a |
| steady frame p95 | 48.690 | 48.690–48.690 | n/a |
| steady frame p99 | 62.270 | 62.270–62.270 | n/a |
| frame split INPUT | 0.077 | 0.077–0.077 | n/a |
| frame split UPDATE | 3.496 | 3.496–3.496 | n/a |
| frame split RENDER | 41.646 | 41.646–41.646 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.119 | 1.119–1.119 | n/a |
| GPU exclusive stage sum | 1.047 | 1.047–1.047 | n/a |
| GPU bakeSunShadowMap | 0.089 | 0.089–0.089 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.006 | 0.006–0.006 | n/a |
| GPU cardinalSurfaceFb | 0.082 | 0.082–0.082 | n/a |
| GPU computeLightVolume | 0.028 | 0.028–0.028 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.009 | 0.009–0.009 | n/a |
| GPU entityCanvasToFb | 0.628 | 0.628–0.628 | n/a |
| GPU fbToScreen | 0.021 | 0.021–0.021 | n/a |
| GPU lightingToTrixel | 0.026 | 0.026–0.026 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.022 | 0.022–0.022 | n/a |
| GPU voxelCardinalElect | 0.024 | 0.024–0.024 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.021 | 0.021–0.021 | n/a |
| GPU voxelStage2 | 0.010 | 0.010–0.010 | n/a |
| GPU voxelSunFaces | 0.067 | 0.067–0.067 | n/a |
| input_to_present mean | 61.896 | 61.896–61.896 | n/a |
| input_to_present p99 | 78.657 | 78.657–78.657 | n/a |
| cursor_to_present mean | 60.612 | 60.612–60.612 | n/a |
| cursor_to_present p99 | 77.495 | 77.495–77.495 | n/a |
| frame_start_to_submit mean | 38.048 | 38.048–38.048 | n/a |
| frame_start_to_submit p99 | 62.245 | 62.245–62.245 | n/a |
| submit_to_present mean | 25.805 | 25.805–25.805 | n/a |
| submit_to_present p99 | 47.864 | 47.864–47.864 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 39.208 | 48.693 | 62.274 | 67.899 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.80 | 2.80–2.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.0926 |

Render device counters: unsupported by this backend.
