| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 14.790 | 14.790–14.790 | n/a |
| frame p95 | 19.160 | 19.160–19.160 | n/a |
| frame p99 | 37.130 | 37.130–37.130 | n/a |
| steady frame avg | 14.210 | 14.210–14.210 | n/a |
| steady frame p95 | 18.390 | 18.390–18.390 | n/a |
| steady frame p99 | 20.410 | 20.410–20.410 | n/a |
| frame split INPUT | 0.002 | 0.002–0.002 | n/a |
| frame split UPDATE | 6.684 | 6.684–6.684 | n/a |
| frame split RENDER | 3.208 | 3.208–3.208 | n/a |
| frame split GPU | 3.785 | 3.785–3.785 | n/a |
| frame split Pacing | 0.334 | 0.334–0.334 | n/a |
| frame split Remainder | 0.778 | 0.778–0.778 | n/a |
| GPU frame commandBufferSpans | 3.785 | 3.785–3.785 | n/a |
| GPU frame envelope | 3.785 | 3.785–3.785 | n/a |
| input_to_present mean | 15.770 | 15.770–15.770 | n/a |
| input_to_present p99 | 22.580 | 22.580–22.580 | n/a |
| cursor_to_present mean | 7.306 | 7.306–7.306 | n/a |
| cursor_to_present p99 | 12.671 | 12.671–12.671 | n/a |
| frame_start_to_submit mean | 9.619 | 9.619–9.619 | n/a |
| frame_start_to_submit p99 | 11.603 | 11.603–11.603 | n/a |
| submit_to_present mean | 4.122 | 4.122–4.122 | n/a |
| submit_to_present p99 | 9.520 | 9.520–9.520 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 14.214 | 18.387 | 20.408 | 21.022 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.90 | 0.90–0.90 | 7 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.0003 |

| Render device counter | Steady per frame, mean over runs | Run min–max | Max in a frame | Run total, mean |
|---|---:|---:|---:|---:|
| bufferPartialWriteAllocations | 0.000 | 0.000–0.000 | 0 | 0.0 |
| bufferPartialWriteAllocatedBytes | 0.000 | 0.000–0.000 | 0 | 0.0 |
| bufferStagedPatches | 4.000 | 4.000–4.000 | 4 | 1200.0 |
| bufferStagedBytes | 148.000 | 148.000–148.000 | 148 | 44400.0 |
| bufferFullReplacements | 15.000 | 15.000–15.000 | 19 | 4504.0 |
| bufferMidFrameWaits | 0.000 | 0.000–0.000 | 0 | 0.0 |

| GPU process allocation peak | Mean MiB | Run min–max MiB |
|---|---:|---:|
| sampled at each frame end | 182.2 | 182.2–182.2 |
