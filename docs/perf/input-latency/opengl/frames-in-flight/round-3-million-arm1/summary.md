| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 31.190 | 31.190–31.190 | n/a |
| frame p95 | 32.120 | 32.120–32.120 | n/a |
| frame p99 | 32.680 | 32.680–32.680 | n/a |
| steady frame avg | 31.220 | 31.220–31.220 | n/a |
| steady frame p95 | 32.160 | 32.160–32.160 | n/a |
| steady frame p99 | 32.460 | 32.460–32.460 | n/a |
| frame split INPUT | 0.020 | 0.020–0.020 | n/a |
| frame split UPDATE | 0.100 | 0.100–0.100 | n/a |
| frame split RENDER | 1.551 | 1.551–1.551 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 29.520 | 29.520–29.520 | n/a |
| input_to_present mean | 62.229 | 62.229–62.229 | n/a |
| input_to_present p99 | 63.889 | 63.889–63.889 | n/a |
| cursor_to_present mean | 62.155 | 62.155–62.155 | n/a |
| cursor_to_present p99 | 63.835 | 63.835–63.835 | n/a |
| frame_start_to_submit mean | 1.764 | 1.764–1.764 | n/a |
| frame_start_to_submit p99 | 3.002 | 3.002–3.002 | n/a |
| submit_to_present mean | 60.576 | 60.576–60.576 | n/a |
| submit_to_present p99 | 62.358 | 62.358–62.358 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 31.216 | 32.163 | 32.460 | 32.688 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.90 | 1.90–1.90 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 0.0821 |

Render device counters: unsupported by this backend.
