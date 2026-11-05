| cell | metric | base mean (min..max, sd) | arm | arm mean (min..max) | arm / base | paired delta mean (sd) | lower 95% | limit | verdict |
|---|---|---|---|---|---|---|---|---|---|
| canvas-stacked-128 | cpu:BakeSunShadowMap | 0.045 (0.033..0.055, 0.010) | per-pixel | 0.043 (0.023..0.059) | 0.951x | -0.002 (0.015) | -0.017 | reported | - |
| canvas-stacked-128 | cpu:CanvasToFramebuffer | 0.144 (0.104..0.176, 0.034) | per-pixel | 0.144 (0.077..0.194) | 0.999x | -0.000 (0.049) | -0.046 | reported | - |
| canvas-stacked-128 | cpu:ComputeSunShadow | 0.019 (0.014..0.023, 0.004) | per-pixel | 0.018 (0.010..0.025) | 0.979x | -0.000 (0.006) | -0.006 | reported | - |
| canvas-stacked-128 | cpu:LightingToTrixel | 0.050 (0.038..0.061, 0.011) | per-pixel | 0.048 (0.025..0.066) | 0.944x | -0.003 (0.017) | -0.019 | reported | - |
| canvas-stacked-128 | cpu:SingleVoxelToCanvasFirst | 0.322 (0.232..0.396, 0.078) | per-pixel | 0.324 (0.171..0.444) | 1.008x | +0.003 (0.115) | -0.107 | reported | - |
| canvas-stacked-128 | gpu:bakeSunShadowMap | 1.458 (1.439..1.476, 0.015) | per-pixel | 3.026 (2.860..3.108) | 2.076x | +1.568 (0.093) | +1.479 | delta <= 0.029 ms | FAIL |
| canvas-stacked-128 | gpu:canvasClear | 0.031 (0.030..0.033, 0.001) | per-pixel | 0.032 (0.031..0.033) | 1.019x | +0.001 (0.001) | +0.000 | reported | - |
| canvas-stacked-128 | gpu:cardinalSurfaceFb | 1.940 (1.797..2.010, 0.088) | per-pixel | 3.525 (3.251..3.669) | 1.817x | +1.585 (0.110) | +1.480 | reported | - |
| canvas-stacked-128 | gpu:computeLightVolume | 0.377 (0.290..0.606, 0.131) | per-pixel | 0.117 (0.109..0.122) | 0.309x | -0.261 (0.135) | -0.390 | reported | - |
| canvas-stacked-128 | gpu:computeSunShadow | 0.047 (0.041..0.051, 0.004) | per-pixel | 0.070 (0.066..0.072) | 1.485x | +0.023 (0.002) | +0.021 | reported | - |
| canvas-stacked-128 | gpu:computeVoxelAO | 0.081 (0.060..0.089, 0.012) | per-pixel | 0.125 (0.097..0.151) | 1.548x | +0.044 (0.028) | +0.018 | reported | - |
| canvas-stacked-128 | gpu:fbToScreen | 0.124 (0.122..0.125, 0.001) | per-pixel | 0.123 (0.121..0.124) | 0.994x | -0.001 (0.001) | -0.002 | reported | - |
| canvas-stacked-128 | gpu:lightingToTrixel | 0.054 (0.048..0.057, 0.004) | per-pixel | 0.054 (0.049..0.056) | 0.993x | -0.000 (0.002) | -0.002 | reported | - |
| canvas-stacked-128 | gpu:trixelToFb | 1.685 (1.588..1.735, 0.061) | per-pixel | 1.309 (1.076..1.425) | 0.777x | -0.376 (0.098) | -0.469 | reported | - |
| canvas-stacked-128 | gpu:voxelCardinalElect | 0.229 (0.220..0.233, 0.005) | per-pixel | 0.348 (0.219..0.421) | 1.522x | +0.119 (0.072) | +0.051 | reported | - |
| canvas-stacked-128 | gpu:voxelCompact | 0.109 (0.058..0.135, 0.032) | per-pixel | 0.103 (0.058..0.127) | 0.952x | -0.005 (0.021) | -0.026 | reported | - |
| canvas-stacked-128 | gpu:voxelStage1 | 0.093 (0.085..0.096, 0.004) | per-pixel | 0.095 (0.088..0.099) | 1.028x | +0.003 (0.002) | +0.001 | reported | - |
| canvas-stacked-128 | gpu:voxelStage2 | 0.214 (0.209..0.217, 0.003) | per-pixel | 0.191 (0.130..0.223) | 0.891x | -0.023 (0.033) | -0.055 | reported | - |
| canvas-stacked-128 | gpu_envelope_ms | 2.444 (2.300..2.519, 0.092) | per-pixel | 4.029 (3.752..4.169) | 1.648x | +1.585 (0.114) | +1.476 | reported | - |
| canvas-stacked-128 | steady_mean_ms | 11.992 (9.970..13.800, 1.755) | per-pixel | 14.868 (10.480..19.350) | 1.240x | +2.876 (3.950) | -0.890 | ratio <= 1.10x | FAIL |
| canvas-stacked-128 | steady_p99_ms | 20.550 (17.930..22.480, 2.139) | per-pixel | 23.776 (22.350..24.510) | 1.157x | +3.226 (1.807) | +1.503 | ratio <= 1.10x | FAIL |
| canvas-stacked-128 | cpu:BakeSunShadowMap | 0.045 (0.033..0.055, 0.010) | fills | 0.074 (0.066..0.081) | 1.649x | +0.029 (0.010) | +0.019 | reported | - |
| canvas-stacked-128 | cpu:CanvasToFramebuffer | 0.144 (0.104..0.176, 0.034) | fills | 0.177 (0.164..0.191) | 1.229x | +0.033 (0.032) | +0.002 | reported | - |
| canvas-stacked-128 | cpu:ComputeSunShadow | 0.019 (0.014..0.023, 0.004) | fills | 0.023 (0.020..0.025) | 1.202x | +0.004 (0.004) | +0.000 | reported | - |
| canvas-stacked-128 | cpu:LightingToTrixel | 0.050 (0.038..0.061, 0.011) | fills | 0.060 (0.054..0.067) | 1.198x | +0.010 (0.011) | -0.000 | reported | - |
| canvas-stacked-128 | cpu:SingleVoxelToCanvasFirst | 0.322 (0.232..0.396, 0.078) | fills | 0.404 (0.367..0.444) | 1.257x | +0.083 (0.073) | +0.013 | reported | - |
| canvas-stacked-128 | gpu:bakeSunShadowMap | 1.458 (1.439..1.476, 0.015) | fills | 3.327 (3.260..3.401) | 2.283x | +1.870 (0.052) | +1.820 | delta <= 0.029 ms | FAIL |
| canvas-stacked-128 | gpu:canvasClear | 0.031 (0.030..0.033, 0.001) | fills | 0.032 (0.030..0.033) | 1.026x | +0.001 (0.002) | -0.001 | reported | - |
| canvas-stacked-128 | gpu:cardinalSurfaceFb | 1.940 (1.797..2.010, 0.088) | fills | 3.844 (3.726..3.918) | 1.981x | +1.904 (0.128) | +1.782 | reported | - |
| canvas-stacked-128 | gpu:computeLightVolume | 0.377 (0.290..0.606, 0.131) | fills | 0.153 (0.135..0.184) | 0.406x | -0.224 (0.131) | -0.349 | reported | - |
| canvas-stacked-128 | gpu:computeSunShadow | 0.047 (0.041..0.051, 0.004) | fills | 0.073 (0.071..0.075) | 1.549x | +0.026 (0.004) | +0.022 | reported | - |
| canvas-stacked-128 | gpu:computeVoxelAO | 0.081 (0.060..0.089, 0.012) | fills | 0.278 (0.261..0.287) | 3.444x | +0.197 (0.016) | +0.182 | reported | - |
| canvas-stacked-128 | gpu:fbToScreen | 0.124 (0.122..0.125, 0.001) | fills | 0.124 (0.123..0.124) | 1.002x | +0.000 (0.001) | -0.001 | reported | - |
| canvas-stacked-128 | gpu:lightingToTrixel | 0.054 (0.048..0.057, 0.004) | fills | 0.056 (0.054..0.059) | 1.033x | +0.002 (0.004) | -0.002 | reported | - |
| canvas-stacked-128 | gpu:trixelToFb | 1.685 (1.588..1.735, 0.061) | fills | 1.513 (1.447..1.565) | 0.898x | -0.172 (0.074) | -0.243 | reported | - |
| canvas-stacked-128 | gpu:voxelCardinalElect | 0.229 (0.220..0.233, 0.005) | fills | 0.165 (0.157..0.173) | 0.721x | -0.064 (0.008) | -0.072 | reported | - |
| canvas-stacked-128 | gpu:voxelCompact | 0.109 (0.058..0.135, 0.032) | fills | 0.113 (0.095..0.135) | 1.039x | +0.004 (0.031) | -0.026 | reported | - |
| canvas-stacked-128 | gpu:voxelStage1 | 0.093 (0.085..0.096, 0.004) | fills | 0.096 (0.094..0.100) | 1.034x | +0.003 (0.005) | -0.001 | reported | - |
| canvas-stacked-128 | gpu:voxelStage2 | 0.214 (0.209..0.217, 0.003) | fills | 0.160 (0.149..0.169) | 0.747x | -0.054 (0.007) | -0.061 | reported | - |
| canvas-stacked-128 | gpu_envelope_ms | 2.444 (2.300..2.519, 0.092) | fills | 4.360 (4.238..4.446) | 1.784x | +1.917 (0.132) | +1.791 | reported | - |
| canvas-stacked-128 | steady_mean_ms | 11.992 (9.970..13.800, 1.755) | fills | 18.708 (16.940..20.500) | 1.560x | +6.716 (1.672) | +5.122 | ratio <= 1.10x | FAIL |
| canvas-stacked-128 | steady_p99_ms | 20.550 (17.930..22.480, 2.139) | fills | 24.928 (24.770..25.080) | 1.213x | +4.378 (2.156) | +2.322 | ratio <= 1.10x | FAIL |
| canvas-stacked-128 | cpu:BakeSunShadowMap | 0.045 (0.033..0.055, 0.010) | head | 0.067 (0.048..0.080) | 1.484x | +0.022 (0.016) | +0.007 | reported | - |
| canvas-stacked-128 | cpu:CanvasToFramebuffer | 0.144 (0.104..0.176, 0.034) | head | 0.161 (0.109..0.203) | 1.112x | +0.016 (0.046) | -0.028 | reported | - |
| canvas-stacked-128 | cpu:ComputeSunShadow | 0.019 (0.014..0.023, 0.004) | head | 0.020 (0.014..0.024) | 1.053x | +0.001 (0.005) | -0.004 | reported | - |
| canvas-stacked-128 | cpu:LightingToTrixel | 0.050 (0.038..0.061, 0.011) | head | 0.053 (0.038..0.065) | 1.060x | +0.003 (0.014) | -0.011 | reported | - |
| canvas-stacked-128 | cpu:SingleVoxelToCanvasFirst | 0.322 (0.232..0.396, 0.078) | head | 0.363 (0.253..0.447) | 1.129x | +0.042 (0.105) | -0.058 | reported | - |
| canvas-stacked-128 | gpu:bakeSunShadowMap | 1.458 (1.439..1.476, 0.015) | head | 4.080 (3.975..4.181) | 2.799x | +2.622 (0.087) | +2.539 | delta <= 0.029 ms | FAIL |
| canvas-stacked-128 | gpu:canvasClear | 0.031 (0.030..0.033, 0.001) | head | 0.030 (0.030..0.031) | 0.981x | -0.001 (0.001) | -0.002 | reported | - |
| canvas-stacked-128 | gpu:cardinalSurfaceFb | 1.940 (1.797..2.010, 0.088) | head | 4.582 (4.412..4.753) | 2.362x | +2.642 (0.174) | +2.476 | reported | - |
| canvas-stacked-128 | gpu:computeLightVolume | 0.377 (0.290..0.606, 0.131) | head | 2.582 (2.514..2.650) | 6.843x | +2.205 (0.176) | +2.037 | reported | - |
| canvas-stacked-128 | gpu:computeSunShadow | 0.047 (0.041..0.051, 0.004) | head | 0.073 (0.064..0.078) | 1.536x | +0.025 (0.009) | +0.017 | reported | - |
| canvas-stacked-128 | gpu:computeVoxelAO | 0.081 (0.060..0.089, 0.012) | head | 0.275 (0.270..0.281) | 3.409x | +0.194 (0.010) | +0.185 | reported | - |
| canvas-stacked-128 | gpu:fbToScreen | 0.124 (0.122..0.125, 0.001) | head | 0.119 (0.114..0.122) | 0.964x | -0.004 (0.004) | -0.008 | reported | - |
| canvas-stacked-128 | gpu:lightingToTrixel | 0.054 (0.048..0.057, 0.004) | head | 0.054 (0.049..0.056) | 0.989x | -0.001 (0.005) | -0.005 | reported | - |
| canvas-stacked-128 | gpu:trixelToFb | 1.685 (1.588..1.735, 0.061) | head | 4.043 (3.900..4.198) | 2.399x | +2.357 (0.138) | +2.226 | reported | - |
| canvas-stacked-128 | gpu:voxelCardinalElect | 0.229 (0.220..0.233, 0.005) | head | 0.158 (0.143..0.172) | 0.691x | -0.071 (0.015) | -0.085 | reported | - |
| canvas-stacked-128 | gpu:voxelCompact | 0.109 (0.058..0.135, 0.032) | head | 0.104 (0.077..0.127) | 0.958x | -0.005 (0.036) | -0.039 | reported | - |
| canvas-stacked-128 | gpu:voxelStage1 | 0.093 (0.085..0.096, 0.004) | head | 0.091 (0.087..0.095) | 0.983x | -0.002 (0.007) | -0.008 | reported | - |
| canvas-stacked-128 | gpu:voxelStage2 | 0.214 (0.209..0.217, 0.003) | head | 0.155 (0.140..0.165) | 0.725x | -0.059 (0.013) | -0.071 | reported | - |
| canvas-stacked-128 | gpu_envelope_ms | 2.444 (2.300..2.519, 0.092) | head | 5.090 (4.880..5.283) | 2.083x | +2.646 (0.194) | +2.461 | reported | - |
| canvas-stacked-128 | steady_mean_ms | 11.992 (9.970..13.800, 1.755) | head | 17.510 (13.380..21.580) | 1.460x | +5.518 (3.647) | +2.041 | ratio <= 1.10x | FAIL |
| canvas-stacked-128 | steady_p99_ms | 20.550 (17.930..22.480, 2.139) | head | 25.852 (25.380..26.360) | 1.258x | +5.302 (1.950) | +3.443 | ratio <= 1.10x | FAIL |

| cell | arm | steady frame-time bins (share of frames) |
|---|---|---|
| canvas-stacked-128 | baseline | 7..8 ms 10%, 8..9 ms 53%, 17..18 ms 13%, 18..19 ms 21% |
| canvas-stacked-128 | per-pixel | 8..9 ms 41%, 18..19 ms 10%, 19..20 ms 16%, 20..21 ms 13% |
| canvas-stacked-128 | fills | 8..9 ms 11%, 18..19 ms 12%, 19..20 ms 21%, 20..21 ms 23% |
| canvas-stacked-128 | head | 8..9 ms 29%, 19..20 ms 9%, 20..21 ms 15%, 21..22 ms 17% |
