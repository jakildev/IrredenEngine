| cell | metric | base mean (min..max, sd) | arm | arm mean (min..max) | arm / base | paired delta mean (sd) | lower 95% | limit | verdict |
|---|---|---|---|---|---|---|---|---|---|
| canvas-lower | cpu:BakeSunShadowMap | 0.056 (0.047..0.060, 0.005) | head | 0.054 (0.041..0.059) | 0.968x | -0.002 (0.011) | -0.012 | reported | - |
| canvas-lower | cpu:CanvasToFramebuffer | 0.169 (0.146..0.180, 0.013) | head | 0.164 (0.131..0.179) | 0.973x | -0.005 (0.026) | -0.030 | reported | - |
| canvas-lower | cpu:ComputeSunShadow | 0.023 (0.020..0.025, 0.002) | head | 0.022 (0.017..0.024) | 0.948x | -0.001 (0.004) | -0.005 | reported | - |
| canvas-lower | cpu:LightingToTrixel | 0.063 (0.053..0.068, 0.006) | head | 0.061 (0.046..0.066) | 0.962x | -0.002 (0.012) | -0.013 | reported | - |
| canvas-lower | cpu:SingleVoxelToCanvasFirst | 0.328 (0.287..0.353, 0.025) | head | 0.320 (0.257..0.352) | 0.975x | -0.008 (0.050) | -0.056 | reported | - |
| canvas-lower | gpu:bakeSunShadowMap | 1.458 (1.452..1.473, 0.008) | head | 1.465 (1.449..1.492) | 1.004x | +0.006 (0.023) | -0.015 | delta <= 0.064 ms | pass (i) |
| canvas-lower | gpu:canvasClear | 0.029 (0.029..0.030, 0.000) | head | 0.029 (0.029..0.030) | 1.007x | +0.000 (0.001) | -0.001 | reported | - |
| canvas-lower | gpu:cardinalSurfaceFb | 1.972 (1.934..2.001, 0.024) | head | 1.985 (1.952..1.999) | 1.007x | +0.013 (0.026) | -0.011 | reported | - |
| canvas-lower | gpu:computeLightVolume | 0.363 (0.336..0.376, 0.018) | head | 0.321 (0.260..0.400) | 0.885x | -0.042 (0.050) | -0.089 | reported | - |
| canvas-lower | gpu:computeSunShadow | 0.053 (0.051..0.055, 0.002) | head | 0.051 (0.050..0.055) | 0.977x | -0.001 (0.003) | -0.004 | reported | - |
| canvas-lower | gpu:computeVoxelAO | 0.087 (0.085..0.088, 0.002) | head | 0.088 (0.085..0.091) | 1.014x | +0.001 (0.001) | +0.000 | reported | - |
| canvas-lower | gpu:fbToScreen | 0.124 (0.124..0.125, 0.000) | head | 0.124 (0.124..0.125) | 1.000x | +0.000 (0.001) | -0.001 | reported | - |
| canvas-lower | gpu:lightingToTrixel | 0.058 (0.055..0.060, 0.002) | head | 0.058 (0.055..0.059) | 1.000x | -0.000 (0.002) | -0.002 | reported | - |
| canvas-lower | gpu:trixelToFb | 1.713 (1.682..1.741, 0.021) | head | 1.718 (1.696..1.727) | 1.003x | +0.005 (0.028) | -0.022 | reported | - |
| canvas-lower | gpu:voxelCardinalElect | 0.218 (0.210..0.224, 0.005) | head | 0.219 (0.216..0.225) | 1.006x | +0.001 (0.006) | -0.005 | reported | - |
| canvas-lower | gpu:voxelCompact | 0.123 (0.111..0.134, 0.008) | head | 0.124 (0.108..0.133) | 1.007x | +0.001 (0.017) | -0.015 | reported | - |
| canvas-lower | gpu:voxelStage1 | 0.092 (0.090..0.093, 0.001) | head | 0.092 (0.091..0.093) | 1.000x | +0.000 (0.001) | -0.001 | reported | - |
| canvas-lower | gpu:voxelStage2 | 0.219 (0.217..0.220, 0.001) | head | 0.220 (0.218..0.220) | 1.005x | +0.001 (0.001) | +0.000 | reported | - |
| canvas-lower | gpu_envelope_ms | 2.486 (2.445..2.510, 0.025) | head | 2.495 (2.479..2.506) | 1.004x | +0.009 (0.029) | -0.018 | reported | - |
| canvas-lower | steady_mean_ms | 8.946 (8.360..9.420, 0.409) | head | 9.144 (8.790..9.550) | 1.022x | +0.198 (0.394) | -0.178 | ratio <= 1.10x | pass |
| canvas-lower | steady_p99_ms | 15.874 (8.840..17.820, 3.935) | head | 17.614 (17.510..17.690) | 1.110x | +1.740 (3.939) | -2.016 | ratio <= 1.10x | FAIL |

| cell | arm | steady frame-time bins (share of frames) |
|---|---|---|
| canvas-lower | baseline | 7..8 ms 27%, 8..9 ms 66%, 16..17 ms 3%, 17..18 ms 4% |
| canvas-lower | head | 7..8 ms 23%, 8..9 ms 67%, 16..17 ms 3%, 17..18 ms 6% |
