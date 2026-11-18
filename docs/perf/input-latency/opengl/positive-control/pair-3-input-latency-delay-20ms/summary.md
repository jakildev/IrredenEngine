| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 34.500 | 34.500–34.500 | n/a |
| frame p95 | 47.180 | 47.180–47.180 | n/a |
| frame p99 | 48.590 | 48.590–48.590 | n/a |
| steady frame avg | 34.420 | 34.420–34.420 | n/a |
| steady frame p95 | 47.140 | 47.140–47.140 | n/a |
| steady frame p99 | 47.940 | 47.940–47.940 | n/a |
| frame split INPUT | 0.037 | 0.037–0.037 | n/a |
| frame split UPDATE | 0.811 | 0.811–0.811 | n/a |
| frame split RENDER | 3.544 | 3.544–3.544 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 30.110 | 30.110–30.110 | n/a |
| input_to_present mean | 41.534 | 41.534–41.534 | n/a |
| input_to_present p99 | 55.881 | 55.881–55.881 | n/a |
| cursor_to_present mean | 10.852 | 10.852–10.852 | n/a |
| cursor_to_present p99 | 14.603 | 14.603–14.603 | n/a |
| frame_start_to_submit mean | 34.388 | 34.388–34.388 | n/a |
| frame_start_to_submit p99 | 47.895 | 47.895–47.895 | n/a |
| submit_to_present mean | 7.628 | 7.628–7.628 | n/a |
| submit_to_present p99 | 11.351 | 11.351–11.351 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 34.423 | 47.141 | 47.941 | 48.585 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.10 | 2.10–2.10 | 6 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 0.4992 |

Render device counters: unsupported by this backend.
