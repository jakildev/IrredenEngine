# metal Debug — production vs queryHierarchy=false

Source `5d93af97cd1aa625995883d8fe641f8330f2e711`, binary `0eebb3615361`, pristine sampler `890ea100e642`.

| Mode | Case | Measurement | Pairs | Production mean (min–max) | Ablation mean (min–max) | Paired delta | Delta sd | One-sided 95% lower / upper | Share | Samples / run |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| counters | far-128 | GPU entityCanvasToFb ms | 5 | 9.325 (9.088–9.539) | 6.014 (5.962–6.073) | +3.311 | 0.174 | +3.145 / +3.477 | 35.5% | 359 |
| counters | far-128 | steady frame avg ms | 5 | 77.812 (76.750–78.580) | 74.176 (73.230–74.740) | +3.636 | 0.161 | +3.483 / +3.789 | 4.7% |  |
| counters | far-128 | frame avg ms | 5 | 78.024 (76.870–78.830) | 74.392 (73.390–75.050) | +3.632 | 0.205 | +3.437 / +3.827 | 4.7% |  |
| counters | far-128 | GPU frame commandBufferSpans ms | 5 | 47.256 (46.871–47.926) | 43.758 (43.557–44.087) | +3.498 | 0.209 | +3.299 / +3.698 | 7.4% |  |
| counters | far-128 | GPU frame envelope ms | 5 | 47.256 (46.871–47.926) | 43.758 (43.557–44.087) | +3.498 | 0.209 | +3.299 / +3.698 | 7.4% |  |
| counters | far-128-filler-first | GPU entityCanvasToFb ms | 5 | 8.935 (8.740–9.085) | 6.488 (6.388–6.569) | +2.446 | 0.175 | +2.280 / +2.613 | 27.4% | 359 |
| counters | far-128-filler-first | steady frame avg ms | 5 | 105.292 (101.850–106.760) | 98.874 (94.650–100.170) | +6.418 | 0.845 | +5.612 / +7.224 | 6.1% |  |
| counters | far-128-filler-first | frame avg ms | 5 | 105.364 (102.110–106.700) | 99.120 (94.830–100.380) | +6.244 | 0.768 | +5.512 / +6.976 | 5.9% |  |
| counters | far-128-filler-first | GPU frame commandBufferSpans ms | 5 | 55.484 (53.518–56.611) | 50.440 (47.739–51.234) | +5.045 | 0.649 | +4.426 / +5.663 | 9.1% |  |
| counters | far-128-filler-first | GPU frame envelope ms | 5 | 55.484 (53.518–56.611) | 50.440 (47.739–51.234) | +5.045 | 0.649 | +4.426 / +5.663 | 9.1% |  |
| counters | far-128-filler-first-frozen | GPU entityCanvasToFb ms | 5 | 6.433 (6.254–6.536) | 6.357 (6.092–6.496) | +0.075 | 0.202 | -0.117 / +0.268 | 1.2% | 359 |
| counters | far-128-filler-first-frozen | steady frame avg ms | 5 | 99.394 (98.550–100.020) | 98.646 (97.000–100.120) | +0.748 | 1.402 | -0.588 / +2.084 | 0.8% |  |
| counters | far-128-filler-first-frozen | frame avg ms | 5 | 99.570 (98.920–100.160) | 99.030 (97.290–100.460) | +0.540 | 1.395 | -0.790 / +1.870 | 0.5% |  |
| counters | far-128-filler-first-frozen | GPU frame commandBufferSpans ms | 5 | 50.697 (49.973–51.286) | 50.213 (48.936–51.473) | +0.484 | 1.074 | -0.540 / +1.508 | 1.0% |  |
| counters | far-128-filler-first-frozen | GPU frame envelope ms | 5 | 50.697 (49.973–51.286) | 50.213 (48.936–51.473) | +0.484 | 1.074 | -0.540 / +1.508 | 1.0% |  |
| counters | far-128-filler-last | GPU entityCanvasToFb ms | 5 | 8.943 (8.804–9.060) | 6.375 (6.185–6.535) | +2.568 | 0.180 | +2.396 / +2.739 | 28.7% | 359 |
| counters | far-128-filler-last | steady frame avg ms | 5 | 105.140 (103.860–106.790) | 97.618 (93.710–99.570) | +7.522 | 1.682 | +5.918 / +9.126 | 7.2% |  |
| counters | far-128-filler-last | frame avg ms | 5 | 105.140 (103.400–106.650) | 97.664 (93.640–99.730) | +7.476 | 1.344 | +6.195 / +8.757 | 7.1% |  |
| counters | far-128-filler-last | GPU frame commandBufferSpans ms | 5 | 55.018 (53.925–55.955) | 49.207 (46.635–50.759) | +5.810 | 0.841 | +5.008 / +6.612 | 10.6% |  |
| counters | far-128-filler-last | GPU frame envelope ms | 5 | 55.018 (53.925–55.955) | 49.207 (46.635–50.759) | +5.810 | 0.841 | +5.008 / +6.612 | 10.6% |  |
| counters | far-128-filler-last-frozen | GPU entityCanvasToFb ms | 5 | 8.676 (8.422–8.998) | 6.375 (6.135–6.492) | +2.301 | 0.265 | +2.049 / +2.553 | 26.5% | 359 |
| counters | far-128-filler-last-frozen | steady frame avg ms | 5 | 104.474 (103.540–105.420) | 98.812 (97.600–99.860) | +5.662 | 1.096 | +4.617 / +6.707 | 5.4% |  |
| counters | far-128-filler-last-frozen | frame avg ms | 5 | 104.434 (103.500–104.920) | 98.792 (97.900–99.940) | +5.642 | 0.783 | +4.895 / +6.389 | 5.4% |  |
| counters | far-128-filler-last-frozen | GPU frame commandBufferSpans ms | 5 | 54.372 (53.860–55.182) | 49.865 (49.293–50.919) | +4.507 | 0.375 | +4.149 / +4.864 | 8.3% |  |
| counters | far-128-filler-last-frozen | GPU frame envelope ms | 5 | 54.372 (53.860–55.182) | 49.865 (49.293–50.919) | +4.507 | 0.375 | +4.149 / +4.864 | 8.3% |  |
| synchronized | far-128 | GPU entityCanvasToFb ms | 5 | 12.097 (11.712–12.492) | 8.838 (8.821–8.862) | +3.259 | 0.291 | +2.981 / +3.537 | 26.9% | 360 |
| synchronized | far-128 | steady frame avg ms | 5 | 85.320 (84.840–85.930) | 81.876 (81.230–82.650) | +3.444 | 0.373 | +3.088 / +3.800 | 4.0% |  |
| synchronized | far-128 | frame avg ms | 5 | 85.444 (84.980–86.120) | 82.104 (81.430–82.850) | +3.340 | 0.452 | +2.909 / +3.771 | 3.9% |  |
| synchronized | far-128 | GPU frame commandBufferSpans ms | 5 | 47.815 (47.470–48.470) | 44.596 (44.142–45.147) | +3.220 | 0.417 | +2.823 / +3.617 | 6.7% |  |
| synchronized | far-128 | GPU frame envelope ms | 5 | 72.821 (72.362–73.427) | 69.534 (68.867–70.247) | +3.287 | 0.380 | +2.925 / +3.649 | 4.5% |  |

Rows are sampled invocation spans or frame averages; they overlap and are never summed. Synchronized rows are finish-bracketed wall intervals.
