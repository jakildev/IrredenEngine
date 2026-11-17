| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 11.310 | 11.310–11.310 | n/a |
| frame p95 | 11.710 | 11.710–11.710 | n/a |
| frame p99 | 12.120 | 12.120–12.120 | n/a |
| steady frame avg | 11.320 | 11.320–11.320 | n/a |
| steady frame p95 | 11.640 | 11.640–11.640 | n/a |
| steady frame p99 | 12.120 | 12.120–12.120 | n/a |
| frame split INPUT | 0.002 | 0.002–0.002 | n/a |
| frame split UPDATE | 0.013 | 0.013–0.013 | n/a |
| frame split RENDER | 0.808 | 0.808–0.808 | n/a |
| frame split GPU | 10.259 | 10.259–10.259 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder | 0.231 | 0.231–0.231 | n/a |
| GPU frame commandBufferSpans | 10.259 | 10.259–10.259 | n/a |
| GPU frame envelope | 10.259 | 10.259–10.259 | n/a |
| GPU exclusive stage sum | 10.373 | 10.373–10.373 | n/a |
| GPU bakeSunShadowMap | 0.108 | 0.108–0.108 | n/a |
| GPU canvasClear | 0.047 | 0.047–0.047 | n/a |
| GPU computeLightVolume | 0.247 | 0.247–0.247 | n/a |
| GPU computeSunShadow | 0.562 | 0.562–0.562 | n/a |
| GPU computeVoxelAO | 0.015 | 0.015–0.015 | n/a |
| GPU computeVoxelAoPerAxis | 0.047 | 0.047–0.047 | n/a |
| GPU fbToScreen | 0.032 | 0.032–0.032 | n/a |
| GPU fogOverflow | 0.009 | 0.009–0.009 | n/a |
| GPU fogPerAxis | 0.025 | 0.025–0.025 | n/a |
| GPU fogToTrixel | 0.011 | 0.011–0.011 | n/a |
| GPU lightingOverflow | 0.122 | 0.122–0.122 | n/a |
| GPU lightingPerAxis | 0.039 | 0.039–0.039 | n/a |
| GPU lightingToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU perAxisCellCompact | 0.079 | 0.079–0.079 | n/a |
| GPU perAxisScatter | 0.651 | 0.651–0.651 | n/a |
| GPU trixelToFb | 0.908 | 0.908–0.908 | n/a |
| GPU voxelCompact | 0.099 | 0.099–0.099 | n/a |
| GPU voxelPerAxisFinalize | 1.365 | 1.365–1.365 | n/a |
| GPU voxelPerAxisOverflow | 2.442 | 2.442–2.442 | n/a |
| GPU voxelPerAxisStore | 0.748 | 0.748–0.748 | n/a |
| GPU voxelSunFaces | 2.801 | 2.801–2.801 | n/a |
| input_to_present mean | 14.786 | 14.786–14.786 | n/a |
| input_to_present p99 | 23.338 | 23.338–23.338 | n/a |
| cursor_to_present mean | 11.155 | 11.155–11.155 | n/a |
| cursor_to_present p99 | 11.895 | 11.895–11.895 | n/a |
| frame_start_to_submit mean | 0.846 | 0.846–0.846 | n/a |
| frame_start_to_submit p99 | 1.462 | 1.462–1.462 | n/a |
| submit_to_present mean | 10.333 | 10.333–10.333 | n/a |
| submit_to_present p99 | 10.716 | 10.716–10.716 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 11.324 | 11.643 | 12.119 | 13.103 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.70 | 0.70–0.70 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 45.000 (0.000) | 51719 / 0 (300) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.0009 |

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
