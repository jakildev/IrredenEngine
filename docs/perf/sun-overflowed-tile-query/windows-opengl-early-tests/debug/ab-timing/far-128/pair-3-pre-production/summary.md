| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 102.820 | 102.820–102.820 | n/a |
| frame p95 | 111.870 | 111.870–111.870 | n/a |
| frame p99 | 185.190 | 185.190–185.190 | n/a |
| steady frame avg | 101.180 | 101.180–101.180 | n/a |
| steady frame p95 | 111.050 | 111.050–111.050 | n/a |
| steady frame p99 | 113.420 | 113.420–113.420 | n/a |
| frame split INPUT | 0.591 | 0.591–0.591 | n/a |
| frame split UPDATE | 7.275 | 7.275–7.275 | n/a |
| frame split RENDER | 38.180 | 38.180–38.180 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 56.774 | 56.774–56.774 | n/a |
| GPU exclusive stage sum | 64.232 | 64.232–64.232 | n/a |
| GPU bakeSunShadowMap | 0.094 | 0.094–0.094 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.010 | 0.010–0.010 | n/a |
| GPU cardinalSurfaceFb | 0.127 | 0.127–0.127 | n/a |
| GPU computeLightVolume | 0.027 | 0.027–0.027 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 | n/a |
| GPU entityCanvasToFb | 63.751 | 63.751–63.751 | n/a |
| GPU fbToScreen | 0.020 | 0.020–0.020 | n/a |
| GPU lightingToTrixel | 0.032 | 0.032–0.032 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.024 | 0.024–0.024 | n/a |
| GPU voxelCompact | 0.015 | 0.015–0.015 | n/a |
| GPU voxelStage1 | 0.023 | 0.023–0.023 | n/a |
| GPU voxelStage2 | 0.013 | 0.013–0.013 | n/a |
| GPU voxelSunFaces | 0.063 | 0.063–0.063 | n/a |
| input_to_present mean | 195.714 | 195.714–195.714 | n/a |
| input_to_present p99 | 212.845 | 212.845–212.845 | n/a |
| cursor_to_present mean | 194.537 | 194.537–194.537 | n/a |
| cursor_to_present p99 | 211.580 | 211.580–211.580 | n/a |
| frame_start_to_submit mean | 40.258 | 40.258–40.258 | n/a |
| frame_start_to_submit p99 | 53.439 | 53.439–53.439 | n/a |
| submit_to_present mean | 161.928 | 161.928–161.928 | n/a |
| submit_to_present p99 | 181.631 | 181.631–181.631 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 101.178 | 111.050 | 113.423 | 114.864 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.10 | 6.10–6.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.1089 |

Render device counters: unsupported by this backend.
