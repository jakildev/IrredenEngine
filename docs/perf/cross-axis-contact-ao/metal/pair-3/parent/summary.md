| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 11.330 | 11.330–11.330 | n/a |
| frame p95 | 11.680 | 11.680–11.680 | n/a |
| frame p99 | 12.240 | 12.240–12.240 | n/a |
| steady frame avg | 11.350 | 11.350–11.350 | n/a |
| steady frame p95 | 11.710 | 11.710–11.710 | n/a |
| steady frame p99 | 12.240 | 12.240–12.240 | n/a |
| frame split INPUT | 0.002 | 0.002–0.002 | n/a |
| frame split UPDATE | 0.013 | 0.013–0.013 | n/a |
| frame split RENDER | 0.824 | 0.824–0.824 | n/a |
| frame split GPU | 10.262 | 10.262–10.262 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder | 0.231 | 0.231–0.231 | n/a |
| GPU frame commandBufferSpans | 10.262 | 10.262–10.262 | n/a |
| GPU frame envelope | 10.262 | 10.262–10.262 | n/a |
| GPU exclusive stage sum | 10.307 | 10.307–10.307 | n/a |
| GPU bakeSunShadowMap | 0.107 | 0.107–0.107 | n/a |
| GPU canvasClear | 0.047 | 0.047–0.047 | n/a |
| GPU computeLightVolume | 0.248 | 0.248–0.248 | n/a |
| GPU computeSunShadow | 0.556 | 0.556–0.556 | n/a |
| GPU computeVoxelAO | 0.015 | 0.015–0.015 | n/a |
| GPU computeVoxelAoPerAxis | 0.031 | 0.031–0.031 | n/a |
| GPU fbToScreen | 0.031 | 0.031–0.031 | n/a |
| GPU fogOverflow | 0.009 | 0.009–0.009 | n/a |
| GPU fogPerAxis | 0.024 | 0.024–0.024 | n/a |
| GPU fogToTrixel | 0.011 | 0.011–0.011 | n/a |
| GPU lightingOverflow | 0.116 | 0.116–0.116 | n/a |
| GPU lightingPerAxis | 0.037 | 0.037–0.037 | n/a |
| GPU lightingToTrixel | 0.016 | 0.016–0.016 | n/a |
| GPU perAxisCellCompact | 0.079 | 0.079–0.079 | n/a |
| GPU perAxisScatter | 0.652 | 0.652–0.652 | n/a |
| GPU trixelToFb | 0.878 | 0.878–0.878 | n/a |
| GPU voxelCompact | 0.100 | 0.100–0.100 | n/a |
| GPU voxelPerAxisFinalize | 1.366 | 1.366–1.366 | n/a |
| GPU voxelPerAxisOverflow | 2.434 | 2.434–2.434 | n/a |
| GPU voxelPerAxisStore | 0.747 | 0.747–0.747 | n/a |
| GPU voxelSunFaces | 2.803 | 2.803–2.803 | n/a |
| input_to_present mean | 14.834 | 14.834–14.834 | n/a |
| input_to_present p99 | 23.358 | 23.358–23.358 | n/a |
| cursor_to_present mean | 11.181 | 11.181–11.181 | n/a |
| cursor_to_present p99 | 12.101 | 12.101–12.101 | n/a |
| frame_start_to_submit mean | 0.882 | 0.882–0.882 | n/a |
| frame_start_to_submit p99 | 1.546 | 1.546–1.546 | n/a |
| submit_to_present mean | 10.328 | 10.328–10.328 | n/a |
| submit_to_present p99 | 10.819 | 10.819–10.819 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 11.347 | 11.706 | 12.236 | 13.197 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.70 | 0.70–0.70 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 45.000 (0.000) | 51719 / 0 (300) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.0004 |

| Render device counter | Steady per frame, mean over runs | Run min–max | Max in a frame | Run total, mean |
|---|---:|---:|---:|---:|
| bufferPartialWriteAllocations | 0.000 | 0.000–0.000 | 0 | 0.0 |
| bufferPartialWriteAllocatedBytes | 0.000 | 0.000–0.000 | 0 | 0.0 |
| bufferStagedPatches | 57.000 | 57.000–57.000 | 57 | 17100.0 |
| bufferStagedBytes | 16412.000 | 16412.000–16412.000 | 16412 | 4923600.0 |
| bufferFullReplacements | 27.000 | 27.000–27.000 | 27 | 8100.0 |
| bufferMidFrameWaits | 0.000 | 0.000–0.000 | 0 | 0.0 |

| GPU process allocation peak | Mean MiB | Run min–max MiB |
|---|---:|---:|
| sampled at each frame end | 735.8 | 735.8–735.8 |
