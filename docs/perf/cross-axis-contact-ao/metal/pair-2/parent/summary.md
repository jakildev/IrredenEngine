| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 11.780 | 11.780–11.780 | n/a |
| frame p95 | 14.950 | 14.950–14.950 | n/a |
| frame p99 | 16.500 | 16.500–16.500 | n/a |
| steady frame avg | 11.400 | 11.400–11.400 | n/a |
| steady frame p95 | 11.750 | 11.750–11.750 | n/a |
| steady frame p99 | 12.570 | 12.570–12.570 | n/a |
| frame split INPUT | 0.003 | 0.003–0.003 | n/a |
| frame split UPDATE | 0.020 | 0.020–0.020 | n/a |
| frame split RENDER | 1.107 | 1.107–1.107 | n/a |
| frame split GPU | 10.353 | 10.353–10.353 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder | 0.301 | 0.301–0.301 | n/a |
| GPU frame commandBufferSpans | 10.353 | 10.353–10.353 | n/a |
| GPU frame envelope | 10.353 | 10.353–10.353 | n/a |
| GPU exclusive stage sum | 10.325 | 10.325–10.325 | n/a |
| GPU bakeSunShadowMap | 0.109 | 0.109–0.109 | n/a |
| GPU canvasClear | 0.047 | 0.047–0.047 | n/a |
| GPU computeLightVolume | 0.252 | 0.252–0.252 | n/a |
| GPU computeSunShadow | 0.552 | 0.552–0.552 | n/a |
| GPU computeVoxelAO | 0.015 | 0.015–0.015 | n/a |
| GPU computeVoxelAoPerAxis | 0.034 | 0.034–0.034 | n/a |
| GPU fbToScreen | 0.033 | 0.033–0.033 | n/a |
| GPU fogOverflow | 0.009 | 0.009–0.009 | n/a |
| GPU fogPerAxis | 0.024 | 0.024–0.024 | n/a |
| GPU fogToTrixel | 0.011 | 0.011–0.011 | n/a |
| GPU lightingOverflow | 0.117 | 0.117–0.117 | n/a |
| GPU lightingPerAxis | 0.039 | 0.039–0.039 | n/a |
| GPU lightingToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU perAxisCellCompact | 0.079 | 0.079–0.079 | n/a |
| GPU perAxisScatter | 0.652 | 0.652–0.652 | n/a |
| GPU trixelToFb | 0.823 | 0.823–0.823 | n/a |
| GPU voxelCompact | 0.109 | 0.109–0.109 | n/a |
| GPU voxelPerAxisFinalize | 1.373 | 1.373–1.373 | n/a |
| GPU voxelPerAxisOverflow | 2.449 | 2.449–2.449 | n/a |
| GPU voxelPerAxisStore | 0.754 | 0.754–0.754 | n/a |
| GPU voxelSunFaces | 2.830 | 2.830–2.830 | n/a |
| input_to_present mean | 14.828 | 14.828–14.828 | n/a |
| input_to_present p99 | 23.182 | 23.182–23.182 | n/a |
| cursor_to_present mean | 11.227 | 11.227–11.227 | n/a |
| cursor_to_present p99 | 12.383 | 12.383–12.383 | n/a |
| frame_start_to_submit mean | 0.876 | 0.876–0.876 | n/a |
| frame_start_to_submit p99 | 1.484 | 1.484–1.484 | n/a |
| submit_to_present mean | 10.381 | 10.381–10.381 | n/a |
| submit_to_present p99 | 10.960 | 10.960–10.960 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 226 | 11.400 | 11.752 | 12.570 | 12.742 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.70 | 0.70–0.70 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 301 / 301 | 0 | 301 | 45.000 (0.000) | 51719 / 0 (301) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 226 / 226 | 0 | 0.0007 |

| Render device counter | Steady per frame, mean over runs | Run min–max | Max in a frame | Run total, mean |
|---|---:|---:|---:|---:|
| bufferPartialWriteAllocations | 0.000 | 0.000–0.000 | 0 | 0.0 |
| bufferPartialWriteAllocatedBytes | 0.000 | 0.000–0.000 | 0 | 0.0 |
| bufferStagedPatches | 57.000 | 57.000–57.000 | 57 | 17157.0 |
| bufferStagedBytes | 16412.000 | 16412.000–16412.000 | 16412 | 4940012.0 |
| bufferFullReplacements | 27.000 | 27.000–27.000 | 27 | 8127.0 |
| bufferMidFrameWaits | 0.000 | 0.000–0.000 | 0 | 0.0 |

| GPU process allocation peak | Mean MiB | Run min–max MiB |
|---|---:|---:|
| sampled at each frame end | 735.8 | 735.8–735.8 |
