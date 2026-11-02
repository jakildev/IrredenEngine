| cell | metric | base mean (min..max, sd) | arm | arm mean (min..max) | arm / base | paired delta mean (sd) | lower 95% | limit | verdict |
|---|---|---|---|---|---|---|---|---|---|
| canvas-default-raster | cpu:BakeSunShadowMap | 0.117 (0.112..0.123, 0.005) | per-pixel | 0.124 (0.116..0.133) | 1.062x | +0.007 (0.002) | +0.005 | reported | - |
| canvas-default-raster | cpu:CanvasToFramebuffer | 0.043 (0.040..0.047, 0.003) | per-pixel | 0.044 (0.042..0.047) | 1.019x | +0.001 (0.001) | -0.000 | reported | - |
| canvas-default-raster | cpu:ComputeSunShadow | 0.006 (0.005..0.006, 0.000) | per-pixel | 0.006 (0.006..0.006) | 1.034x | +0.000 (0.000) | -0.000 | reported | - |
| canvas-default-raster | cpu:LightingToTrixel | 0.133 (0.127..0.141, 0.006) | per-pixel | 0.141 (0.132..0.152) | 1.059x | +0.008 (0.003) | +0.005 | reported | - |
| canvas-default-raster | cpu:SingleVoxelToCanvasFirst | 1.778 (1.685..1.949, 0.116) | per-pixel | 1.851 (1.762..1.978) | 1.041x | +0.073 (0.071) | +0.005 | reported | - |
| canvas-default-raster | gpu:bakeSunShadowMap | 1.378 (1.338..1.448, 0.045) | per-pixel | 2.689 (2.666..2.719) | 1.952x | +1.311 (0.052) | +1.261 | reported | - |
| canvas-default-raster | gpu:canvasClear | 0.009 (0.008..0.009, 0.001) | per-pixel | 0.008 (0.008..0.009) | 0.953x | -0.000 (0.001) | -0.001 | reported | - |
| canvas-default-raster | gpu:cardinalSurfaceFb | 1.467 (1.417..1.571, 0.063) | per-pixel | 2.744 (2.715..2.773) | 1.870x | +1.277 (0.073) | +1.207 | reported | - |
| canvas-default-raster | gpu:computeLightVolume | 0.044 (0.042..0.046, 0.002) | per-pixel | 0.287 (0.273..0.306) | 6.493x | +0.243 (0.012) | +0.232 | reported | - |
| canvas-default-raster | gpu:computeSunShadow | 0.025 (0.024..0.028, 0.002) | per-pixel | 0.027 (0.026..0.028) | 1.063x | +0.002 (0.002) | +0.000 | reported | - |
| canvas-default-raster | gpu:computeVoxelAO | 0.095 (0.089..0.101, 0.005) | per-pixel | 0.210 (0.194..0.216) | 2.197x | +0.114 (0.008) | +0.107 | reported | - |
| canvas-default-raster | gpu:entityCanvasToFb | 1.915 (1.857..2.046, 0.079) | per-pixel | 3.181 (3.145..3.219) | 1.661x | +1.266 (0.090) | +1.180 | reported | - |
| canvas-default-raster | gpu:fbToScreen | 0.032 (0.030..0.034, 0.002) | per-pixel | 0.030 (0.029..0.031) | 0.949x | -0.002 (0.002) | -0.003 | reported | - |
| canvas-default-raster | gpu:lightingToTrixel | 0.017 (0.017..0.018, 0.001) | per-pixel | 0.017 (0.017..0.019) | 1.000x | +0.000 (0.001) | -0.001 | reported | - |
| canvas-default-raster | gpu:shapeDepth | 0.082 (0.078..0.088, 0.004) | per-pixel | 0.077 (0.075..0.078) | 0.939x | -0.005 (0.005) | -0.010 | reported | - |
| canvas-default-raster | gpu:shapeOwnerClear | 0.007 (0.007..0.009, 0.001) | per-pixel | 0.007 (0.006..0.007) | 0.892x | -0.001 (0.001) | -0.002 | reported | - |
| canvas-default-raster | gpu:shapeOwnerElect | 0.081 (0.077..0.089, 0.005) | per-pixel | 0.077 (0.075..0.079) | 0.951x | -0.004 (0.005) | -0.009 | reported | - |
| canvas-default-raster | gpu:shapePublish | 0.083 (0.080..0.090, 0.004) | per-pixel | 0.077 (0.075..0.080) | 0.928x | -0.006 (0.005) | -0.011 | reported | - |
| canvas-default-raster | gpu:trixelToFb | 0.965 (0.935..1.031, 0.040) | per-pixel | 2.315 (2.242..2.347) | 2.400x | +1.351 (0.041) | +1.312 | reported | - |
| canvas-default-raster | gpu:voxelCardinalElect | 0.014 (0.013..0.014, 0.001) | per-pixel | 0.013 (0.012..0.013) | 0.941x | -0.001 (0.000) | -0.001 | reported | - |
| canvas-default-raster | gpu:voxelCompact | 0.034 (0.034..0.035, 0.001) | per-pixel | 0.033 (0.033..0.034) | 0.971x | -0.001 (0.001) | -0.002 | reported | - |
| canvas-default-raster | gpu:voxelStage1 | 0.030 (0.029..0.031, 0.001) | per-pixel | 0.028 (0.027..0.029) | 0.934x | -0.002 (0.001) | -0.003 | reported | - |
| canvas-default-raster | gpu:voxelStage2 | 0.015 (0.014..0.015, 0.000) | per-pixel | 0.014 (0.014..0.014) | 0.946x | -0.001 (0.000) | -0.001 | reported | - |
| canvas-default-raster | gpu_envelope_ms | 5.633 (5.541..5.752, 0.104) | per-pixel | 7.003 (6.869..7.184) | 1.243x | +1.370 (0.120) | +1.255 | reported | - |
| canvas-default-raster | steady_mean_ms | 20.466 (19.500..21.620, 1.033) | per-pixel | 22.226 (21.250..23.610) | 1.086x | +1.760 (0.189) | +1.580 | ratio <= 1.10x | pass |
| canvas-default-raster | steady_p99_ms | 24.550 (21.500..28.560, 2.815) | per-pixel | 24.820 (23.260..27.470) | 1.011x | +0.270 (1.845) | -1.489 | ratio <= 1.10x | pass |
| canvas-default-raster | cpu:BakeSunShadowMap | 0.117 (0.112..0.123, 0.005) | head | 0.131 (0.124..0.135) | 1.118x | +0.014 (0.005) | +0.009 | reported | - |
| canvas-default-raster | cpu:CanvasToFramebuffer | 0.043 (0.040..0.047, 0.003) | head | 0.043 (0.040..0.045) | 1.005x | +0.000 (0.003) | -0.002 | reported | - |
| canvas-default-raster | cpu:ComputeSunShadow | 0.006 (0.005..0.006, 0.000) | head | 0.006 (0.006..0.006) | 1.034x | +0.000 (0.000) | -0.000 | reported | - |
| canvas-default-raster | cpu:LightingToTrixel | 0.133 (0.127..0.141, 0.006) | head | 0.143 (0.135..0.149) | 1.071x | +0.009 (0.006) | +0.004 | reported | - |
| canvas-default-raster | cpu:SingleVoxelToCanvasFirst | 1.778 (1.685..1.949, 0.116) | head | 1.864 (1.796..1.920) | 1.048x | +0.086 (0.100) | -0.010 | reported | - |
| canvas-default-raster | gpu:bakeSunShadowMap | 1.378 (1.338..1.448, 0.045) | head | 2.563 (2.518..2.602) | 1.860x | +1.185 (0.072) | +1.117 | reported | - |
| canvas-default-raster | gpu:canvasClear | 0.009 (0.008..0.009, 0.001) | head | 0.008 (0.008..0.009) | 0.953x | -0.000 (0.001) | -0.001 | reported | - |
| canvas-default-raster | gpu:cardinalSurfaceFb | 1.467 (1.417..1.571, 0.063) | head | 2.608 (2.559..2.648) | 1.778x | +1.141 (0.090) | +1.055 | reported | - |
| canvas-default-raster | gpu:computeLightVolume | 0.044 (0.042..0.046, 0.002) | head | 0.183 (0.146..0.205) | 4.136x | +0.139 (0.021) | +0.118 | reported | - |
| canvas-default-raster | gpu:computeSunShadow | 0.025 (0.024..0.028, 0.002) | head | 0.027 (0.026..0.028) | 1.047x | +0.001 (0.001) | -0.000 | reported | - |
| canvas-default-raster | gpu:computeVoxelAO | 0.095 (0.089..0.101, 0.005) | head | 0.083 (0.082..0.085) | 0.870x | -0.012 (0.006) | -0.018 | reported | - |
| canvas-default-raster | gpu:entityCanvasToFb | 1.915 (1.857..2.046, 0.079) | head | 3.058 (2.997..3.104) | 1.597x | +1.143 (0.102) | +1.046 | reported | - |
| canvas-default-raster | gpu:fbToScreen | 0.032 (0.030..0.034, 0.002) | head | 0.030 (0.029..0.031) | 0.956x | -0.001 (0.002) | -0.003 | reported | - |
| canvas-default-raster | gpu:lightingToTrixel | 0.017 (0.017..0.018, 0.001) | head | 0.018 (0.017..0.018) | 1.023x | +0.000 (0.001) | -0.000 | reported | - |
| canvas-default-raster | gpu:shapeDepth | 0.082 (0.078..0.088, 0.004) | head | 0.078 (0.074..0.080) | 0.953x | -0.004 (0.004) | -0.008 | reported | - |
| canvas-default-raster | gpu:shapeOwnerClear | 0.007 (0.007..0.009, 0.001) | head | 0.006 (0.005..0.008) | 0.865x | -0.001 (0.001) | -0.002 | reported | - |
| canvas-default-raster | gpu:shapeOwnerElect | 0.081 (0.077..0.089, 0.005) | head | 0.077 (0.074..0.079) | 0.951x | -0.004 (0.005) | -0.009 | reported | - |
| canvas-default-raster | gpu:shapePublish | 0.083 (0.080..0.090, 0.004) | head | 0.079 (0.076..0.081) | 0.952x | -0.004 (0.005) | -0.008 | reported | - |
| canvas-default-raster | gpu:trixelToFb | 0.965 (0.935..1.031, 0.040) | head | 2.165 (2.112..2.222) | 2.244x | +1.200 (0.074) | +1.129 | reported | - |
| canvas-default-raster | gpu:voxelCardinalElect | 0.014 (0.013..0.014, 0.001) | head | 0.013 (0.013..0.013) | 0.956x | -0.001 (0.001) | -0.001 | reported | - |
| canvas-default-raster | gpu:voxelCompact | 0.034 (0.034..0.035, 0.001) | head | 0.034 (0.033..0.035) | 0.983x | -0.001 (0.001) | -0.002 | reported | - |
| canvas-default-raster | gpu:voxelStage1 | 0.030 (0.029..0.031, 0.001) | head | 0.028 (0.028..0.029) | 0.934x | -0.002 (0.001) | -0.003 | reported | - |
| canvas-default-raster | gpu:voxelStage2 | 0.015 (0.014..0.015, 0.000) | head | 0.014 (0.014..0.015) | 0.959x | -0.001 (0.001) | -0.001 | reported | - |
| canvas-default-raster | gpu_envelope_ms | 5.633 (5.541..5.752, 0.104) | head | 6.885 (6.764..7.001) | 1.222x | +1.252 (0.143) | +1.115 | reported | - |
| canvas-default-raster | steady_mean_ms | 20.466 (19.500..21.620, 1.033) | head | 22.440 (21.440..23.210) | 1.096x | +1.974 (0.877) | +1.138 | ratio <= 1.10x | pass |
| canvas-default-raster | steady_p99_ms | 24.550 (21.500..28.560, 2.815) | head | 25.202 (23.820..26.590) | 1.027x | +0.652 (2.640) | -1.865 | ratio <= 1.10x | pass |

| cell | arm | steady frame-time bins (share of frames) |
|---|---|---|
| canvas-default-raster | baseline | 18..19 ms 11%, 19..20 ms 39%, 20..21 ms 23%, 21..22 ms 14% |
| canvas-default-raster | per-pixel | 20..21 ms 19%, 21..22 ms 30%, 22..23 ms 24%, 23..24 ms 15% |
| canvas-default-raster | head | 20..21 ms 14%, 21..22 ms 22%, 22..23 ms 33%, 23..24 ms 16% |
