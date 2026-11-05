| cell | metric | base mean (min..max, sd) | arm | arm mean (min..max) | arm / base | paired delta mean (sd) | lower 95% | limit | verdict |
|---|---|---|---|---|---|---|---|---|---|
| canvas-stacked-128 | cpu:BakeSunShadowMap | 0.049 (0.043..0.054, 0.005) | cooperative | 0.063 (0.025..0.079) | 1.290x | +0.014 (0.025) | -0.010 | reported | - |
| canvas-stacked-128 | cpu:CanvasToFramebuffer | 0.150 (0.132..0.163, 0.013) | cooperative | 0.137 (0.063..0.168) | 0.915x | -0.013 (0.052) | -0.062 | reported | - |
| canvas-stacked-128 | cpu:ComputeSunShadow | 0.020 (0.018..0.022, 0.002) | cooperative | 0.018 (0.007..0.022) | 0.881x | -0.002 (0.007) | -0.009 | reported | - |
| canvas-stacked-128 | cpu:LightingToTrixel | 0.056 (0.049..0.061, 0.005) | cooperative | 0.050 (0.019..0.062) | 0.886x | -0.006 (0.021) | -0.026 | reported | - |
| canvas-stacked-128 | cpu:SingleVoxelToCanvasFirst | 0.334 (0.294..0.361, 0.030) | cooperative | 0.319 (0.137..0.395) | 0.956x | -0.015 (0.123) | -0.132 | reported | - |
| canvas-stacked-128 | gpu:bakeSunShadowMap | 1.449 (1.437..1.460, 0.012) | cooperative | 4.080 (3.968..4.191) | 2.815x | +2.630 (0.082) | +2.552 | delta <= 0.029 ms | FAIL |
| canvas-stacked-128 | gpu:canvasClear | 0.033 (0.033..0.034, 0.001) | cooperative | 0.033 (0.032..0.033) | 0.982x | -0.001 (0.001) | -0.001 | reported | - |
| canvas-stacked-128 | gpu:cardinalSurfaceFb | 1.783 (1.766..1.803, 0.017) | cooperative | 4.429 (4.335..4.533) | 2.483x | +2.645 (0.074) | +2.575 | reported | - |
| canvas-stacked-128 | gpu:computeLightVolume | 0.503 (0.362..0.606, 0.120) | cooperative | 2.309 (2.150..2.471) | 4.593x | +1.806 (0.128) | +1.684 | reported | - |
| canvas-stacked-128 | gpu:computeSunShadow | 0.041 (0.040..0.041, 0.001) | cooperative | 0.062 (0.061..0.063) | 1.527x | +0.021 (0.001) | +0.021 | reported | - |
| canvas-stacked-128 | gpu:computeVoxelAO | 0.067 (0.061..0.078, 0.007) | cooperative | 0.291 (0.287..0.295) | 4.318x | +0.224 (0.007) | +0.217 | reported | - |
| canvas-stacked-128 | gpu:fbToScreen | 0.122 (0.122..0.122, 0.000) | cooperative | 0.121 (0.118..0.122) | 0.989x | -0.001 (0.002) | -0.003 | reported | - |
| canvas-stacked-128 | gpu:lightingToTrixel | 0.048 (0.048..0.049, 0.001) | cooperative | 0.048 (0.047..0.049) | 1.000x | +0.000 (0.001) | -0.001 | reported | - |
| canvas-stacked-128 | gpu:trixelToFb | 1.563 (1.539..1.584, 0.022) | cooperative | 3.915 (3.860..4.007) | 2.506x | +2.353 (0.057) | +2.298 | reported | - |
| canvas-stacked-128 | gpu:voxelCardinalElect | 0.219 (0.211..0.222, 0.004) | cooperative | 0.140 (0.135..0.144) | 0.640x | -0.079 (0.007) | -0.085 | reported | - |
| canvas-stacked-128 | gpu:voxelCompact | 0.058 (0.058..0.059, 0.000) | cooperative | 0.058 (0.056..0.059) | 0.997x | -0.000 (0.001) | -0.001 | reported | - |
| canvas-stacked-128 | gpu:voxelStage1 | 0.085 (0.085..0.086, 0.000) | cooperative | 0.086 (0.084..0.087) | 1.009x | +0.001 (0.002) | -0.001 | reported | - |
| canvas-stacked-128 | gpu:voxelStage2 | 0.210 (0.208..0.211, 0.001) | cooperative | 0.135 (0.132..0.137) | 0.643x | -0.075 (0.003) | -0.077 | reported | - |
| canvas-stacked-128 | gpu_envelope_ms | 2.289 (2.275..2.304, 0.013) | cooperative | 4.931 (4.808..5.048) | 2.154x | +2.642 (0.092) | +2.554 | reported | - |
| canvas-stacked-128 | steady_mean_ms | 12.924 (12.050..13.880, 0.763) | cooperative | 17.330 (9.220..20.360) | 1.341x | +4.406 (4.924) | -0.289 | ratio <= 1.10x | FAIL |
| canvas-stacked-128 | steady_p99_ms | 19.566 (18.020..20.780, 1.196) | cooperative | 24.028 (20.500..25.140) | 1.228x | +4.462 (1.862) | +2.687 | ratio <= 1.10x | FAIL |
| canvas-stacked-128 | cpu:BakeSunShadowMap | 0.049 (0.043..0.054, 0.005) | head | 0.065 (0.056..0.077) | 1.335x | +0.016 (0.011) | +0.006 | reported | - |
| canvas-stacked-128 | cpu:CanvasToFramebuffer | 0.150 (0.132..0.163, 0.013) | head | 0.141 (0.128..0.159) | 0.940x | -0.009 (0.020) | -0.028 | reported | - |
| canvas-stacked-128 | cpu:ComputeSunShadow | 0.020 (0.018..0.022, 0.002) | head | 0.019 (0.017..0.023) | 0.960x | -0.001 (0.003) | -0.004 | reported | - |
| canvas-stacked-128 | cpu:LightingToTrixel | 0.056 (0.049..0.061, 0.005) | head | 0.052 (0.045..0.061) | 0.925x | -0.004 (0.009) | -0.013 | reported | - |
| canvas-stacked-128 | cpu:SingleVoxelToCanvasFirst | 0.334 (0.294..0.361, 0.030) | head | 0.318 (0.284..0.365) | 0.954x | -0.015 (0.048) | -0.061 | reported | - |
| canvas-stacked-128 | gpu:bakeSunShadowMap | 1.449 (1.437..1.460, 0.012) | head | 2.106 (2.096..2.111) | 1.453x | +0.656 (0.012) | +0.645 | delta <= 0.029 ms | FAIL |
| canvas-stacked-128 | gpu:canvasClear | 0.033 (0.033..0.034, 0.001) | head | 0.033 (0.033..0.034) | 0.994x | -0.000 (0.001) | -0.001 | reported | - |
| canvas-stacked-128 | gpu:cardinalSurfaceFb | 1.783 (1.766..1.803, 0.017) | head | 2.452 (2.443..2.458) | 1.375x | +0.668 (0.016) | +0.653 | reported | - |
| canvas-stacked-128 | gpu:computeLightVolume | 0.503 (0.362..0.606, 0.120) | head | 0.115 (0.112..0.119) | 0.229x | -0.388 (0.121) | -0.503 | reported | - |
| canvas-stacked-128 | gpu:computeSunShadow | 0.041 (0.040..0.041, 0.001) | head | 0.054 (0.053..0.054) | 1.325x | +0.013 (0.000) | +0.013 | reported | - |
| canvas-stacked-128 | gpu:computeVoxelAO | 0.067 (0.061..0.078, 0.007) | head | 0.292 (0.284..0.300) | 4.335x | +0.225 (0.008) | +0.217 | reported | - |
| canvas-stacked-128 | gpu:fbToScreen | 0.122 (0.122..0.122, 0.000) | head | 0.122 (0.122..0.122) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-stacked-128 | gpu:lightingToTrixel | 0.048 (0.048..0.049, 0.001) | head | 0.050 (0.049..0.050) | 1.029x | +0.001 (0.001) | +0.001 | reported | - |
| canvas-stacked-128 | gpu:trixelToFb | 1.563 (1.539..1.584, 0.022) | head | 1.219 (1.213..1.223) | 0.780x | -0.343 (0.022) | -0.364 | reported | - |
| canvas-stacked-128 | gpu:voxelCardinalElect | 0.219 (0.211..0.222, 0.004) | head | 0.139 (0.137..0.144) | 0.638x | -0.079 (0.006) | -0.084 | reported | - |
| canvas-stacked-128 | gpu:voxelCompact | 0.058 (0.058..0.059, 0.000) | head | 0.059 (0.058..0.061) | 1.014x | +0.001 (0.001) | -0.000 | reported | - |
| canvas-stacked-128 | gpu:voxelStage1 | 0.085 (0.085..0.086, 0.000) | head | 0.087 (0.087..0.088) | 1.023x | +0.002 (0.001) | +0.001 | reported | - |
| canvas-stacked-128 | gpu:voxelStage2 | 0.210 (0.208..0.211, 0.001) | head | 0.137 (0.135..0.138) | 0.653x | -0.073 (0.002) | -0.075 | reported | - |
| canvas-stacked-128 | gpu_envelope_ms | 2.289 (2.275..2.304, 0.013) | head | 2.961 (2.948..2.973) | 1.293x | +0.672 (0.016) | +0.657 | reported | - |
| canvas-stacked-128 | steady_mean_ms | 12.924 (12.050..13.880, 0.763) | head | 14.342 (12.930..15.820) | 1.110x | +1.418 (1.876) | -0.370 | ratio <= 1.10x | FAIL |
| canvas-stacked-128 | steady_p99_ms | 19.566 (18.020..20.780, 1.196) | head | 22.104 (21.990..22.270) | 1.130x | +2.538 (1.243) | +1.353 | ratio <= 1.10x | FAIL |

| cell | arm | steady frame-time bins (share of frames) |
|---|---|---|
| canvas-stacked-128 | baseline | 7..8 ms 12%, 8..9 ms 38%, 16..17 ms 6%, 17..18 ms 40% |
| canvas-stacked-128 | cooperative | 8..9 ms 26%, 19..20 ms 17%, 20..21 ms 25%, 23..24 ms 8% |
| canvas-stacked-128 | head | 7..8 ms 8%, 8..9 ms 32%, 17..18 ms 15%, 18..19 ms 39% |
