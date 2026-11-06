# opengl Debug — production vs queryHierarchy=false

Source `5d93af97cd1aa625995883d8fe641f8330f2e711`, binary `f3bcc111c3d2`, pristine sampler `4bb016193aeb`.

| Mode | Case | Measurement | Pairs | Production mean (min–max) | Ablation mean (min–max) | Paired delta | Delta sd | One-sided 95% lower / upper | Share | Samples / run |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| counters | far-128 | GPU entityCanvasToFb ms | 5 | 62.443 (60.622–65.039) | 0.764 (0.648–0.859) | +61.679 | 1.741 | +60.019 / +63.338 | 98.8% | 357/359 |
| counters | far-128 | steady frame avg ms | 5 | 88.652 (86.320–93.260) | 36.828 (33.960–37.940) | +51.824 | 3.093 | +48.874 / +54.774 | 58.5% |  |
| counters | far-128 | frame avg ms | 5 | 90.020 (87.950–93.240) | 42.290 (39.440–44.060) | +47.730 | 2.556 | +45.293 / +50.167 | 53.0% |  |
| counters | far-128-filler-first | GPU entityCanvasToFb ms | 5 | 46.030 (43.776–48.182) | 0.927 (0.904–0.992) | +45.103 | 1.776 | +43.410 / +46.796 | 98.0% | 358/359 |
| counters | far-128-filler-first | steady frame avg ms | 5 | 81.784 (77.970–86.770) | 77.610 (73.930–83.480) | +4.174 | 5.924 | -1.475 / +9.823 | 5.1% |  |
| counters | far-128-filler-first | frame avg ms | 5 | 92.108 (89.010–96.770) | 87.314 (84.250–91.680) | +4.794 | 4.259 | +0.733 / +8.855 | 5.2% |  |
| counters | far-128-filler-first-frozen | GPU entityCanvasToFb ms | 5 | 0.964 (0.938–1.010) | 0.912 (0.883–0.956) | +0.052 | 0.052 | +0.002 / +0.102 | 5.4% | 359 |
| counters | far-128-filler-first-frozen | steady frame avg ms | 5 | 80.874 (76.500–82.850) | 79.854 (77.970–85.800) | +1.020 | 3.819 | -2.621 / +4.661 | 1.3% |  |
| counters | far-128-filler-first-frozen | frame avg ms | 5 | 90.750 (86.160–94.160) | 89.726 (87.350–94.500) | +1.024 | 4.221 | -3.000 / +5.048 | 1.1% |  |
| counters | far-128-filler-last | GPU entityCanvasToFb ms | 5 | 48.628 (45.138–56.249) | 0.919 (0.871–0.958) | +47.709 | 4.624 | +43.300 / +52.118 | 98.1% | 358/359 |
| counters | far-128-filler-last | steady frame avg ms | 5 | 84.632 (79.580–89.440) | 83.026 (79.780–86.130) | +1.606 | 4.893 | -3.059 / +6.271 | 1.9% |  |
| counters | far-128-filler-last | frame avg ms | 5 | 94.374 (90.550–97.930) | 93.202 (90.690–96.050) | +1.172 | 4.012 | -2.653 / +4.997 | 1.2% |  |
| counters | far-128-filler-last-frozen | GPU entityCanvasToFb ms | 5 | 46.619 (45.144–48.388) | 0.871 (0.827–0.936) | +45.748 | 1.247 | +44.559 / +46.936 | 98.1% | 357/358/359 |
| counters | far-128-filler-last-frozen | steady frame avg ms | 5 | 81.506 (78.610–83.640) | 83.640 (79.750–90.910) | -2.134 | 3.102 | -5.091 / +0.823 | -2.6% |  |
| counters | far-128-filler-last-frozen | frame avg ms | 5 | 90.612 (88.600–91.800) | 92.478 (88.400–98.280) | -1.866 | 2.784 | -4.520 / +0.788 | -2.1% |  |
| synchronized | far-128 | GPU entityCanvasToFb ms | 5 | 64.303 (62.754–65.447) | 2.309 (2.243–2.393) | +61.993 | 0.981 | +61.058 / +62.929 | 96.4% | 360 |
| synchronized | far-128 | steady frame avg ms | 5 | 101.690 (100.060–102.910) | 35.960 (35.550–36.700) | +65.730 | 1.059 | +64.721 / +66.739 | 64.6% |  |
| synchronized | far-128 | frame avg ms | 5 | 106.490 (105.020–107.650) | 40.212 (39.560–40.520) | +66.278 | 1.158 | +65.174 / +67.382 | 62.2% |  |

Rows are sampled invocation spans or frame averages; they overlap and are never summed. Synchronized rows are finish-bracketed wall intervals.
