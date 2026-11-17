| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 28.680 | 28.680–28.680 | n/a |
| frame p95 | 28.720 | 28.720–28.720 | n/a |
| frame p99 | 29.240 | 29.240–29.240 | n/a |
| steady frame avg | 27.680 | 27.680–27.680 | n/a |
| steady frame p95 | 28.700 | 28.700–28.700 | n/a |
| steady frame p99 | 28.950 | 28.950–28.950 | n/a |
| frame split INPUT | 0.004 | 0.004–0.004 | n/a |
| frame split UPDATE | 3.395 | 3.395–3.395 | n/a |
| frame split RENDER | 5.183 | 5.183–5.183 | n/a |
| frame split GPU | 18.709 | 18.709–18.709 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder | 1.389 | 1.389–1.389 | n/a |
| GPU frame commandBufferSpans | 18.709 | 18.709–18.709 | n/a |
| GPU frame envelope | 18.709 | 18.709–18.709 | n/a |
| input_to_present mean | 26.179 | 26.179–26.179 | n/a |
| input_to_present p99 | 26.833 | 26.833–26.833 | n/a |
| cursor_to_present mean | 24.197 | 24.197–24.197 | n/a |
| cursor_to_present p99 | 24.665 | 24.665–24.665 | n/a |
| frame_start_to_submit mean | 8.462 | 8.462–8.462 | n/a |
| frame_start_to_submit p99 | 9.724 | 9.724–9.724 | n/a |
| submit_to_present mean | 19.094 | 19.094–19.094 | n/a |
| submit_to_present p99 | 19.580 | 19.580–19.580 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 27.679 | 28.704 | 28.950 | 29.177 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.70 | 1.70–1.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.0005 |

| Render device counter | Steady per frame, mean over runs | Run min–max | Max in a frame | Run total, mean |
|---|---:|---:|---:|---:|
| bufferPartialWriteAllocations | 0.000 | 0.000–0.000 | 3 | 3.0 |
| bufferPartialWriteAllocatedBytes | 0.000 | 0.000–0.000 | 41943040 | 41943040.0 |
| bufferStagedPatches | 6.000 | 6.000–6.000 | 9 | 1803.0 |
| bufferStagedBytes | 16015776.000 | 16015776.000–16015776.000 | 36140776 | 4824857800.0 |
| bufferFullReplacements | 13.000 | 13.000–13.000 | 14 | 3901.0 |
| bufferMidFrameWaits | 0.000 | 0.000–0.000 | 0 | 0.0 |

| GPU process allocation peak | Mean MiB | Run min–max MiB |
|---|---:|---:|
| sampled at each frame end | 313.7 | 313.7–313.7 |
