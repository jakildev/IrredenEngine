| cell | metric | base mean (min..max, sd) | arm | arm mean (min..max) | arm / base | paired delta mean (sd) | lower 95% | limit | verdict |
|---|---|---|---|---|---|---|---|---|---|
| canvas-stacked-128 | cpu:BakeSunShadowMap | 0.047 (0.035..0.054, 0.008) | per-pixel | 0.047 (0.034..0.057) | 1.000x | +0.000 (0.013) | -0.013 | reported | - |
| canvas-stacked-128 | cpu:CanvasToFramebuffer | 0.151 (0.117..0.174, 0.023) | per-pixel | 0.157 (0.119..0.192) | 1.041x | +0.006 (0.040) | -0.032 | reported | - |
| canvas-stacked-128 | cpu:ComputeSunShadow | 0.020 (0.015..0.023, 0.003) | per-pixel | 0.020 (0.014..0.024) | 1.010x | +0.000 (0.005) | -0.005 | reported | - |
| canvas-stacked-128 | cpu:LightingToTrixel | 0.053 (0.040..0.061, 0.008) | per-pixel | 0.053 (0.038..0.066) | 1.008x | +0.000 (0.015) | -0.014 | reported | - |
| canvas-stacked-128 | cpu:SingleVoxelToCanvasFirst | 0.332 (0.257..0.384, 0.052) | per-pixel | 0.352 (0.266..0.433) | 1.060x | +0.020 (0.091) | -0.067 | reported | - |
| canvas-stacked-128 | gpu:bakeSunShadowMap | 1.466 (1.461..1.477, 0.007) | per-pixel | 3.071 (3.030..3.098) | 2.094x | +1.605 (0.023) | +1.583 | delta <= 0.029 ms | FAIL |
| canvas-stacked-128 | gpu:canvasClear | 0.031 (0.030..0.032, 0.001) | per-pixel | 0.031 (0.029..0.035) | 1.006x | +0.000 (0.003) | -0.002 | reported | - |
| canvas-stacked-128 | gpu:cardinalSurfaceFb | 1.975 (1.913..2.011, 0.040) | per-pixel | 3.598 (3.519..3.644) | 1.822x | +1.623 (0.044) | +1.581 | reported | - |
| canvas-stacked-128 | gpu:computeLightVolume | 0.371 (0.317..0.457, 0.058) | per-pixel | 0.119 (0.117..0.121) | 0.321x | -0.252 (0.059) | -0.308 | reported | - |
| canvas-stacked-128 | gpu:computeSunShadow | 0.049 (0.049..0.051, 0.001) | per-pixel | 0.076 (0.070..0.095) | 1.538x | +0.027 (0.010) | +0.017 | reported | - |
| canvas-stacked-128 | gpu:computeVoxelAO | 0.081 (0.075..0.087, 0.006) | per-pixel | 0.118 (0.091..0.148) | 1.452x | +0.037 (0.021) | +0.017 | reported | - |
| canvas-stacked-128 | gpu:fbToScreen | 0.124 (0.123..0.124, 0.000) | per-pixel | 0.125 (0.121..0.135) | 1.011x | +0.001 (0.006) | -0.004 | reported | - |
| canvas-stacked-128 | gpu:lightingToTrixel | 0.055 (0.052..0.057, 0.002) | per-pixel | 0.056 (0.053..0.058) | 1.011x | +0.001 (0.002) | -0.001 | reported | - |
| canvas-stacked-128 | gpu:trixelToFb | 1.713 (1.670..1.740, 0.029) | per-pixel | 1.371 (1.289..1.455) | 0.800x | -0.342 (0.062) | -0.402 | reported | - |
| canvas-stacked-128 | gpu:voxelCardinalElect | 0.232 (0.227..0.236, 0.004) | per-pixel | 0.379 (0.335..0.438) | 1.629x | +0.146 (0.044) | +0.104 | reported | - |
| canvas-stacked-128 | gpu:voxelCompact | 0.113 (0.091..0.128, 0.016) | per-pixel | 0.112 (0.092..0.133) | 0.991x | -0.001 (0.022) | -0.022 | reported | - |
| canvas-stacked-128 | gpu:voxelStage1 | 0.094 (0.091..0.095, 0.002) | per-pixel | 0.098 (0.094..0.099) | 1.043x | +0.004 (0.001) | +0.003 | reported | - |
| canvas-stacked-128 | gpu:voxelStage2 | 0.214 (0.212..0.216, 0.001) | per-pixel | 0.206 (0.187..0.228) | 0.965x | -0.007 (0.015) | -0.022 | reported | - |
| canvas-stacked-128 | gpu_envelope_ms | 2.470 (2.406..2.507, 0.044) | per-pixel | 4.109 (4.023..4.136) | 1.663x | +1.639 (0.031) | +1.609 | reported | - |
| canvas-stacked-128 | steady_mean_ms | 12.208 (10.820..13.220, 0.982) | per-pixel | 16.584 (13.860..19.250) | 1.358x | +4.376 (2.674) | +1.826 | ratio <= 1.10x | FAIL |
| canvas-stacked-128 | steady_p99_ms | 21.566 (19.900..22.190, 0.946) | per-pixel | 24.302 (24.070..24.650) | 1.127x | +2.736 (1.148) | +1.642 | ratio <= 1.10x | FAIL |
| canvas-stacked-128 | cpu:BakeSunShadowMap | 0.047 (0.035..0.054, 0.008) | fills | 0.061 (0.040..0.079) | 1.298x | +0.014 (0.015) | -0.000 | reported | - |
| canvas-stacked-128 | cpu:CanvasToFramebuffer | 0.151 (0.117..0.174, 0.023) | fills | 0.145 (0.094..0.184) | 0.958x | -0.006 (0.031) | -0.036 | reported | - |
| canvas-stacked-128 | cpu:ComputeSunShadow | 0.020 (0.015..0.023, 0.003) | fills | 0.018 (0.012..0.024) | 0.939x | -0.001 (0.004) | -0.005 | reported | - |
| canvas-stacked-128 | cpu:LightingToTrixel | 0.053 (0.040..0.061, 0.008) | fills | 0.049 (0.032..0.064) | 0.928x | -0.004 (0.013) | -0.016 | reported | - |
| canvas-stacked-128 | cpu:SingleVoxelToCanvasFirst | 0.332 (0.257..0.384, 0.052) | fills | 0.322 (0.211..0.410) | 0.970x | -0.010 (0.070) | -0.077 | reported | - |
| canvas-stacked-128 | gpu:bakeSunShadowMap | 1.466 (1.461..1.477, 0.007) | fills | 3.253 (3.171..3.344) | 2.219x | +1.787 (0.066) | +1.724 | delta <= 0.029 ms | FAIL |
| canvas-stacked-128 | gpu:canvasClear | 0.031 (0.030..0.032, 0.001) | fills | 0.032 (0.030..0.033) | 1.013x | +0.000 (0.001) | -0.001 | reported | - |
| canvas-stacked-128 | gpu:cardinalSurfaceFb | 1.975 (1.913..2.011, 0.040) | fills | 3.724 (3.554..3.868) | 1.886x | +1.749 (0.108) | +1.646 | reported | - |
| canvas-stacked-128 | gpu:computeLightVolume | 0.371 (0.317..0.457, 0.058) | fills | 0.150 (0.129..0.175) | 0.405x | -0.221 (0.066) | -0.283 | reported | - |
| canvas-stacked-128 | gpu:computeSunShadow | 0.049 (0.049..0.051, 0.001) | fills | 0.073 (0.068..0.079) | 1.478x | +0.024 (0.004) | +0.020 | reported | - |
| canvas-stacked-128 | gpu:computeVoxelAO | 0.081 (0.075..0.087, 0.006) | fills | 0.282 (0.267..0.292) | 3.467x | +0.201 (0.008) | +0.193 | reported | - |
| canvas-stacked-128 | gpu:fbToScreen | 0.124 (0.123..0.124, 0.000) | fills | 0.122 (0.115..0.127) | 0.985x | -0.002 (0.004) | -0.006 | reported | - |
| canvas-stacked-128 | gpu:lightingToTrixel | 0.055 (0.052..0.057, 0.002) | fills | 0.054 (0.050..0.057) | 0.986x | -0.001 (0.004) | -0.004 | reported | - |
| canvas-stacked-128 | gpu:trixelToFb | 1.713 (1.670..1.740, 0.029) | fills | 1.448 (1.321..1.531) | 0.845x | -0.265 (0.085) | -0.347 | reported | - |
| canvas-stacked-128 | gpu:voxelCardinalElect | 0.232 (0.227..0.236, 0.004) | fills | 0.156 (0.144..0.170) | 0.673x | -0.076 (0.007) | -0.082 | reported | - |
| canvas-stacked-128 | gpu:voxelCompact | 0.113 (0.091..0.128, 0.016) | fills | 0.093 (0.067..0.111) | 0.824x | -0.020 (0.022) | -0.041 | reported | - |
| canvas-stacked-128 | gpu:voxelStage1 | 0.094 (0.091..0.095, 0.002) | fills | 0.093 (0.088..0.096) | 0.994x | -0.001 (0.003) | -0.004 | reported | - |
| canvas-stacked-128 | gpu:voxelStage2 | 0.214 (0.212..0.216, 0.001) | fills | 0.156 (0.142..0.167) | 0.732x | -0.057 (0.010) | -0.067 | reported | - |
| canvas-stacked-128 | gpu_envelope_ms | 2.470 (2.406..2.507, 0.044) | fills | 4.231 (4.070..4.382) | 1.713x | +1.761 (0.094) | +1.672 | reported | - |
| canvas-stacked-128 | steady_mean_ms | 12.208 (10.820..13.220, 0.982) | fills | 14.586 (10.010..19.120) | 1.195x | +2.378 (3.209) | -0.681 | ratio <= 1.10x | FAIL |
| canvas-stacked-128 | steady_p99_ms | 21.566 (19.900..22.190, 0.946) | fills | 24.320 (23.490..25.040) | 1.128x | +2.754 (0.688) | +2.098 | ratio <= 1.10x | FAIL |
| canvas-stacked-128 | cpu:BakeSunShadowMap | 0.047 (0.035..0.054, 0.008) | head | 0.071 (0.061..0.078) | 1.511x | +0.024 (0.009) | +0.015 | reported | - |
| canvas-stacked-128 | cpu:CanvasToFramebuffer | 0.151 (0.117..0.174, 0.023) | head | 0.172 (0.135..0.199) | 1.142x | +0.021 (0.036) | -0.013 | reported | - |
| canvas-stacked-128 | cpu:ComputeSunShadow | 0.020 (0.015..0.023, 0.003) | head | 0.021 (0.017..0.023) | 1.061x | +0.001 (0.004) | -0.003 | reported | - |
| canvas-stacked-128 | cpu:LightingToTrixel | 0.053 (0.040..0.061, 0.008) | head | 0.056 (0.047..0.063) | 1.065x | +0.003 (0.009) | -0.006 | reported | - |
| canvas-stacked-128 | cpu:SingleVoxelToCanvasFirst | 0.332 (0.257..0.384, 0.052) | head | 0.387 (0.306..0.439) | 1.166x | +0.055 (0.078) | -0.019 | reported | - |
| canvas-stacked-128 | gpu:bakeSunShadowMap | 1.466 (1.461..1.477, 0.007) | head | 4.497 (4.285..4.656) | 3.066x | +3.030 (0.149) | +2.888 | delta <= 0.029 ms | FAIL |
| canvas-stacked-128 | gpu:canvasClear | 0.031 (0.030..0.032, 0.001) | head | 0.030 (0.028..0.033) | 0.962x | -0.001 (0.001) | -0.003 | reported | - |
| canvas-stacked-128 | gpu:cardinalSurfaceFb | 1.975 (1.913..2.011, 0.040) | head | 4.980 (4.679..5.237) | 2.522x | +3.006 (0.219) | +2.797 | reported | - |
| canvas-stacked-128 | gpu:computeLightVolume | 0.371 (0.317..0.457, 0.058) | head | 2.986 (2.844..3.195) | 8.048x | +2.615 (0.138) | +2.484 | reported | - |
| canvas-stacked-128 | gpu:computeSunShadow | 0.049 (0.049..0.051, 0.001) | head | 0.079 (0.063..0.092) | 1.603x | +0.030 (0.014) | +0.017 | reported | - |
| canvas-stacked-128 | gpu:computeVoxelAO | 0.081 (0.075..0.087, 0.006) | head | 0.270 (0.253..0.296) | 3.319x | +0.189 (0.019) | +0.170 | reported | - |
| canvas-stacked-128 | gpu:fbToScreen | 0.124 (0.123..0.124, 0.000) | head | 0.118 (0.110..0.121) | 0.950x | -0.006 (0.005) | -0.011 | reported | - |
| canvas-stacked-128 | gpu:lightingToTrixel | 0.055 (0.052..0.057, 0.002) | head | 0.052 (0.047..0.056) | 0.938x | -0.003 (0.006) | -0.009 | reported | - |
| canvas-stacked-128 | gpu:trixelToFb | 1.713 (1.670..1.740, 0.029) | head | 4.579 (4.299..4.812) | 2.672x | +2.865 (0.198) | +2.677 | reported | - |
| canvas-stacked-128 | gpu:voxelCardinalElect | 0.232 (0.227..0.236, 0.004) | head | 0.152 (0.136..0.165) | 0.652x | -0.081 (0.014) | -0.094 | reported | - |
| canvas-stacked-128 | gpu:voxelCompact | 0.113 (0.091..0.128, 0.016) | head | 0.097 (0.058..0.119) | 0.854x | -0.017 (0.033) | -0.048 | reported | - |
| canvas-stacked-128 | gpu:voxelStage1 | 0.094 (0.091..0.095, 0.002) | head | 0.088 (0.082..0.093) | 0.942x | -0.005 (0.005) | -0.010 | reported | - |
| canvas-stacked-128 | gpu:voxelStage2 | 0.214 (0.212..0.216, 0.001) | head | 0.151 (0.138..0.162) | 0.705x | -0.063 (0.012) | -0.074 | reported | - |
| canvas-stacked-128 | gpu_envelope_ms | 2.470 (2.406..2.507, 0.044) | head | 5.481 (5.145..5.751) | 2.219x | +3.011 (0.240) | +2.783 | reported | - |
| canvas-stacked-128 | steady_mean_ms | 12.208 (10.820..13.220, 0.982) | head | 19.048 (15.570..22.060) | 1.560x | +6.840 (2.889) | +4.085 | ratio <= 1.10x | FAIL |
| canvas-stacked-128 | steady_p99_ms | 21.566 (19.900..22.190, 0.946) | head | 26.204 (25.500..26.910) | 1.215x | +4.638 (0.760) | +3.913 | ratio <= 1.10x | FAIL |

| cell | arm | steady frame-time bins (share of frames) |
|---|---|---|
| canvas-stacked-128 | baseline | 7..8 ms 13%, 8..9 ms 48%, 17..18 ms 13%, 18..19 ms 22% |
| canvas-stacked-128 | per-pixel | 8..9 ms 29%, 18..19 ms 10%, 19..20 ms 23%, 20..21 ms 17% |
| canvas-stacked-128 | fills | 8..9 ms 42%, 18..19 ms 11%, 19..20 ms 16%, 20..21 ms 11% |
| canvas-stacked-128 | head | 8..9 ms 16%, 19..20 ms 10%, 20..21 ms 15%, 21..22 ms 17% |
