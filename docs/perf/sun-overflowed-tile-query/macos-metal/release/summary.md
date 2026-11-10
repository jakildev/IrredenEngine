# metal Release — production vs queryHierarchy=false

Source `5d93af97cd1aa625995883d8fe641f8330f2e711`, binary `23e419510623`, pristine sampler `890ea100e642`.

| Mode | Case | Measurement | Pairs | Production mean (min–max) | Ablation mean (min–max) | Paired delta | Delta sd | One-sided 95% lower / upper | Share | Samples / run |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| counters | far-128 | GPU entityCanvasToFb ms | 5 | 9.085 (8.789–9.408) | 6.038 (6.012–6.105) | +3.046 | 0.252 | +2.806 / +3.286 | 33.5% | 359 |
| counters | far-128 | steady frame avg ms | 5 | 77.562 (76.660–77.990) | 74.266 (73.310–74.690) | +3.296 | 0.182 | +3.122 / +3.470 | 4.2% |  |
| counters | far-128 | frame avg ms | 5 | 77.716 (76.800–78.140) | 74.526 (73.670–75.010) | +3.190 | 0.301 | +2.903 / +3.477 | 4.1% |  |
| counters | far-128 | GPU frame commandBufferSpans ms | 5 | 47.574 (47.354–47.835) | 44.485 (44.362–44.700) | +3.089 | 0.277 | +2.825 / +3.353 | 6.5% |  |
| counters | far-128 | GPU frame envelope ms | 5 | 47.574 (47.354–47.835) | 44.485 (44.362–44.700) | +3.089 | 0.277 | +2.825 / +3.353 | 6.5% |  |
| counters | far-128-filler-first | GPU entityCanvasToFb ms | 5 | 8.989 (8.906–9.113) | 6.455 (6.338–6.539) | +2.534 | 0.117 | +2.423 / +2.646 | 28.2% | 359 |
| counters | far-128-filler-first | steady frame avg ms | 5 | 104.950 (101.970–106.150) | 99.466 (97.360–100.140) | +5.484 | 0.677 | +4.838 / +6.130 | 5.2% |  |
| counters | far-128-filler-first | frame avg ms | 5 | 104.988 (101.890–106.220) | 99.748 (97.750–100.590) | +5.240 | 0.793 | +4.484 / +5.996 | 5.0% |  |
| counters | far-128-filler-first | GPU frame commandBufferSpans ms | 5 | 55.954 (54.274–56.736) | 51.622 (50.602–52.075) | +4.332 | 0.620 | +3.740 / +4.924 | 7.7% |  |
| counters | far-128-filler-first | GPU frame envelope ms | 5 | 55.954 (54.274–56.736) | 51.622 (50.602–52.075) | +4.332 | 0.620 | +3.740 / +4.924 | 7.7% |  |
| counters | far-128-filler-first-frozen | GPU entityCanvasToFb ms | 5 | 6.430 (6.254–6.547) | 6.382 (6.139–6.515) | +0.049 | 0.178 | -0.121 / +0.218 | 0.8% | 359 |
| counters | far-128-filler-first-frozen | steady frame avg ms | 5 | 99.048 (98.700–99.660) | 99.280 (98.340–99.800) | -0.232 | 0.481 | -0.691 / +0.227 | -0.2% |  |
| counters | far-128-filler-first-frozen | frame avg ms | 5 | 99.416 (98.980–100.110) | 99.658 (98.820–100.160) | -0.242 | 0.402 | -0.625 / +0.141 | -0.2% |  |
| counters | far-128-filler-first-frozen | GPU frame commandBufferSpans ms | 5 | 51.110 (50.737–51.514) | 51.270 (50.445–51.785) | -0.160 | 0.424 | -0.565 / +0.245 | -0.3% |  |
| counters | far-128-filler-first-frozen | GPU frame envelope ms | 5 | 51.110 (50.737–51.514) | 51.270 (50.445–51.785) | -0.160 | 0.424 | -0.565 / +0.245 | -0.3% |  |
| counters | far-128-filler-last | GPU entityCanvasToFb ms | 5 | 8.861 (8.494–8.999) | 6.523 (6.287–6.609) | +2.338 | 0.087 | +2.255 / +2.422 | 26.4% | 359 |
| counters | far-128-filler-last | steady frame avg ms | 5 | 104.676 (103.920–107.180) | 98.416 (95.460–101.660) | +6.260 | 1.553 | +4.779 / +7.741 | 6.0% |  |
| counters | far-128-filler-last | frame avg ms | 5 | 104.524 (103.620–106.540) | 98.542 (95.790–101.340) | +5.982 | 1.691 | +4.370 / +7.594 | 5.7% |  |
| counters | far-128-filler-last | GPU frame commandBufferSpans ms | 5 | 55.256 (54.748–56.151) | 50.324 (48.199–51.993) | +4.932 | 1.286 | +3.706 / +6.158 | 8.9% |  |
| counters | far-128-filler-last | GPU frame envelope ms | 5 | 55.256 (54.748–56.151) | 50.324 (48.199–51.993) | +4.932 | 1.286 | +3.706 / +6.158 | 8.9% |  |
| counters | far-128-filler-last-frozen | GPU entityCanvasToFb ms | 5 | 8.844 (8.637–9.001) | 6.498 (6.432–6.548) | +2.346 | 0.120 | +2.232 / +2.460 | 26.5% | 359 |
| counters | far-128-filler-last-frozen | steady frame avg ms | 5 | 103.606 (102.190–104.590) | 98.984 (98.150–101.040) | +4.622 | 2.056 | +2.661 / +6.583 | 4.5% |  |
| counters | far-128-filler-last-frozen | frame avg ms | 5 | 103.722 (102.740–104.710) | 99.032 (98.310–100.740) | +4.690 | 1.581 | +3.182 / +6.198 | 4.5% |  |
| counters | far-128-filler-last-frozen | GPU frame commandBufferSpans ms | 5 | 54.768 (53.994–55.560) | 50.740 (50.335–51.394) | +4.028 | 0.878 | +3.191 / +4.865 | 7.4% |  |
| counters | far-128-filler-last-frozen | GPU frame envelope ms | 5 | 54.768 (53.994–55.560) | 50.740 (50.335–51.394) | +4.028 | 0.878 | +3.191 / +4.865 | 7.4% |  |
| synchronized | far-128 | GPU entityCanvasToFb ms | 5 | 12.121 (11.921–12.466) | 8.752 (8.602–8.848) | +3.369 | 0.198 | +3.180 / +3.558 | 27.8% | 360 |
| synchronized | far-128 | steady frame avg ms | 5 | 85.656 (85.230–85.980) | 82.000 (81.800–82.210) | +3.656 | 0.177 | +3.488 / +3.824 | 4.3% |  |
| synchronized | far-128 | frame avg ms | 5 | 85.882 (85.470–86.240) | 82.232 (82.060–82.500) | +3.650 | 0.274 | +3.388 / +3.912 | 4.3% |  |
| synchronized | far-128 | GPU frame commandBufferSpans ms | 5 | 48.631 (48.226–48.979) | 45.103 (44.859–45.333) | +3.529 | 0.314 | +3.229 / +3.828 | 7.3% |  |
| synchronized | far-128 | GPU frame envelope ms | 5 | 73.307 (72.900–73.629) | 69.824 (69.707–70.017) | +3.482 | 0.232 | +3.261 / +3.704 | 4.8% |  |

Rows are sampled invocation spans or frame averages; they overlap and are never summed. Synchronized rows are finish-bracketed wall intervals.
