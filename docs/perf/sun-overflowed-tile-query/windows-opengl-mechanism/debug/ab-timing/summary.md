# opengl Debug — interleaved binary / variant A/B

Source `45a220089c6b843cab5fb09007ae34356b2df275`; pre binary `1b6310828d70` (reverts `84d6f7bc93de19e3c1586f27b4330387aa9ac011`); post binary `8c3e90d1541a`.

| Case | Measurement | Contrast | Pairs | After mean (min–max) | Before mean (min–max) | Paired delta | One-sided 95% lower / upper | Delta / before | Upper / before |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| far-128 | GPU entityCanvasToFb ms | post vs pre, production | 5 | 48.009 (47.898–48.285) | 65.652 (65.052–66.330) | -17.643 | -18.309 / -16.977 | -26.9% | -25.9% |
| far-128 | GPU entityCanvasToFb ms | post vs pre, ablation | 5 | 0.976 (0.659–1.374) | 0.992 (0.756–1.272) | -0.016 | -0.108 / +0.076 | -1.6% | 7.7% |
| far-128 | GPU entityCanvasToFb ms | query term, pre | 5 | 65.652 (65.052–66.330) | 0.992 (0.756–1.272) | +64.660 | +63.970 / +65.350 | 6515.5% | 6585.0% |
| far-128 | GPU entityCanvasToFb ms | query term, post | 5 | 48.009 (47.898–48.285) | 0.976 (0.659–1.374) | +47.033 | +46.847 / +47.219 | 4817.9% | 4837.0% |
| far-128 | steady frame avg ms | post vs pre, production | 5 | 78.284 (77.950–78.670) | 95.900 (95.080–96.900) | -17.616 | -18.330 / -16.902 | -18.4% | -17.6% |
| far-128 | steady frame avg ms | post vs pre, ablation | 5 | 38.868 (33.760–44.520) | 39.806 (37.750–43.020) | -0.938 | -2.933 / +1.057 | -2.4% | 2.7% |
| far-128 | steady frame avg ms | query term, pre | 5 | 95.900 (95.080–96.900) | 39.806 (37.750–43.020) | +56.094 | +53.650 / +58.538 | 140.9% | 147.1% |
| far-128 | steady frame avg ms | query term, post | 5 | 78.284 (77.950–78.670) | 38.868 (33.760–44.520) | +39.416 | +35.736 / +43.096 | 101.4% | 110.9% |
| far-128-filler-last | GPU entityCanvasToFb ms | post vs pre, production | 5 | 40.374 (40.055–40.747) | 50.345 (49.974–50.954) | -9.970 | -10.403 / -9.538 | -19.8% | -18.9% |
| far-128-filler-last | GPU entityCanvasToFb ms | post vs pre, ablation | 5 | 1.269 (1.160–1.411) | 1.139 (1.016–1.244) | +0.130 | +0.089 / +0.171 | 11.4% | 15.0% |
| far-128-filler-last | GPU entityCanvasToFb ms | query term, pre | 5 | 50.345 (49.974–50.954) | 1.139 (1.016–1.244) | +49.205 | +48.816 / +49.595 | 4319.3% | 4353.5% |
| far-128-filler-last | GPU entityCanvasToFb ms | query term, post | 5 | 40.374 (40.055–40.747) | 1.269 (1.160–1.411) | +39.105 | +38.820 / +39.390 | 3081.1% | 3103.5% |
| far-128-filler-last | steady frame avg ms | post vs pre, production | 5 | 108.736 (86.110–130.690) | 110.752 (85.890–142.020) | -2.016 | -10.431 / +6.399 | -1.8% | 5.8% |
| far-128-filler-last | steady frame avg ms | post vs pre, ablation | 5 | 108.082 (91.350–133.340) | 108.672 (91.400–133.620) | -0.590 | -2.777 / +1.597 | -0.5% | 1.5% |
| far-128-filler-last | steady frame avg ms | query term, pre | 5 | 110.752 (85.890–142.020) | 108.672 (91.400–133.620) | +2.080 | -5.188 / +9.348 | 1.9% | 8.6% |
| far-128-filler-last | steady frame avg ms | query term, post | 5 | 108.736 (86.110–130.690) | 108.082 (91.350–133.340) | +0.654 | -3.853 / +5.161 | 0.6% | 4.8% |
| far-128-filler-first | GPU entityCanvasToFb ms | post vs pre, production | 5 | 38.661 (38.508–38.815) | 47.731 (47.103–48.253) | -9.070 | -9.602 / -8.539 | -19.0% | -17.9% |
| far-128-filler-first | GPU entityCanvasToFb ms | post vs pre, ablation | 5 | 1.362 (1.176–1.527) | 1.150 (1.064–1.228) | +0.212 | +0.122 / +0.302 | 18.4% | 26.3% |
| far-128-filler-first | GPU entityCanvasToFb ms | query term, pre | 5 | 47.731 (47.103–48.253) | 1.150 (1.064–1.228) | +46.581 | +46.100 / +47.062 | 4049.8% | 4091.6% |
| far-128-filler-first | GPU entityCanvasToFb ms | query term, post | 5 | 38.661 (38.508–38.815) | 1.362 (1.176–1.527) | +37.298 | +37.169 / +37.427 | 2737.7% | 2747.2% |
| far-128-filler-first | steady frame avg ms | post vs pre, production | 5 | 111.742 (90.850–139.040) | 111.078 (85.790–133.620) | +0.664 | -5.187 / +6.515 | 0.6% | 5.9% |
| far-128-filler-first | steady frame avg ms | post vs pre, ablation | 5 | 113.114 (92.780–138.100) | 108.998 (91.930–133.420) | +4.116 | -9.240 / +17.472 | 3.8% | 16.0% |
| far-128-filler-first | steady frame avg ms | query term, pre | 5 | 111.078 (85.790–133.620) | 108.998 (91.930–133.420) | +2.080 | -10.045 / +14.205 | 1.9% | 13.0% |
| far-128-filler-first | steady frame avg ms | query term, post | 5 | 111.742 (90.850–139.040) | 113.114 (92.780–138.100) | -1.372 | -7.222 / +4.478 | -1.2% | 4.0% |
| far-128-filler-last-frozen | GPU entityCanvasToFb ms | post vs pre, production | 5 | 39.811 (39.423–40.249) | 49.823 (49.556–50.293) | -10.012 | -10.496 / -9.528 | -20.1% | -19.1% |
| far-128-filler-last-frozen | GPU entityCanvasToFb ms | post vs pre, ablation | 5 | 1.227 (1.106–1.340) | 1.079 (0.952–1.198) | +0.148 | +0.124 / +0.171 | 13.7% | 15.9% |
| far-128-filler-last-frozen | GPU entityCanvasToFb ms | query term, pre | 5 | 49.823 (49.556–50.293) | 1.079 (0.952–1.198) | +48.743 | +48.388 / +49.098 | 4515.8% | 4548.7% |
| far-128-filler-last-frozen | GPU entityCanvasToFb ms | query term, post | 5 | 39.811 (39.423–40.249) | 1.227 (1.106–1.340) | +38.584 | +38.253 / +38.915 | 3144.5% | 3171.5% |
| far-128-filler-last-frozen | steady frame avg ms | post vs pre, production | 5 | 114.500 (94.190–134.980) | 114.622 (93.730–138.350) | -0.122 | -2.644 / +2.400 | -0.1% | 2.1% |
| far-128-filler-last-frozen | steady frame avg ms | post vs pre, ablation | 5 | 114.636 (93.820–135.560) | 109.464 (92.170–128.920) | +5.172 | +0.866 / +9.478 | 4.7% | 8.7% |
| far-128-filler-last-frozen | steady frame avg ms | query term, pre | 5 | 114.622 (93.730–138.350) | 109.464 (92.170–128.920) | +5.158 | +0.220 / +10.096 | 4.7% | 9.2% |
| far-128-filler-last-frozen | steady frame avg ms | query term, post | 5 | 114.500 (94.190–134.980) | 114.636 (93.820–135.560) | -0.136 | -1.605 / +1.333 | -0.1% | 1.2% |
| far-128-filler-first-frozen | GPU entityCanvasToFb ms | post vs pre, production | 5 | 1.386 (1.275–1.646) | 1.251 (1.092–1.399) | +0.134 | +0.061 / +0.208 | 10.7% | 16.6% |
| far-128-filler-first-frozen | GPU entityCanvasToFb ms | post vs pre, ablation | 5 | 1.301 (1.207–1.418) | 1.124 (1.007–1.280) | +0.177 | +0.148 / +0.206 | 15.8% | 18.4% |
| far-128-filler-first-frozen | GPU entityCanvasToFb ms | query term, pre | 5 | 1.251 (1.092–1.399) | 1.124 (1.007–1.280) | +0.128 | +0.096 / +0.160 | 11.4% | 14.2% |
| far-128-filler-first-frozen | GPU entityCanvasToFb ms | query term, post | 5 | 1.386 (1.275–1.646) | 1.301 (1.207–1.418) | +0.085 | +0.008 / +0.162 | 6.6% | 12.5% |
| far-128-filler-first-frozen | steady frame avg ms | post vs pre, production | 5 | 112.322 (93.840–134.190) | 112.658 (91.660–128.910) | -0.336 | -5.790 / +5.118 | -0.3% | 4.5% |
| far-128-filler-first-frozen | steady frame avg ms | post vs pre, ablation | 5 | 110.032 (99.600–133.340) | 107.180 (87.860–137.810) | +2.852 | -5.095 / +10.799 | 2.7% | 10.1% |
| far-128-filler-first-frozen | steady frame avg ms | query term, pre | 5 | 112.658 (91.660–128.910) | 107.180 (87.860–137.810) | +5.478 | -5.275 / +16.231 | 5.1% | 15.1% |
| far-128-filler-first-frozen | steady frame avg ms | query term, post | 5 | 112.322 (93.840–134.190) | 110.032 (99.600–133.340) | +2.290 | -3.465 / +8.045 | 2.1% | 7.3% |

`far-128` production composite cut: upper bound -25.9% of the pre mean; the half-cut gate (≤ −50.0%) is not met.

Columns read after = the first arm named in the contrast, before = the second. Rows are sampled invocation spans or frame averages; they are never summed.
