| cell | metric | baseline mean (min..max) | head mean (min..max) | head / baseline |
|---|---|---|---|---|
| canvas-stacked-128 | cpu:BakeSunShadowMap | 0.029 (0.024..0.033) | 0.032 (0.026..0.035) | 1.118x |
| canvas-stacked-128 | cpu:CanvasToFramebuffer | 0.096 (0.077..0.118) | 0.110 (0.091..0.123) | 1.148x |
| canvas-stacked-128 | cpu:ComputeSunShadow | 0.012 (0.010..0.013) | 0.013 (0.011..0.015) | 1.119x |
| canvas-stacked-128 | cpu:LightingToTrixel | 0.032 (0.026..0.037) | 0.036 (0.030..0.041) | 1.138x |
| canvas-stacked-128 | cpu:SingleVoxelToCanvasFirst | 0.211 (0.171..0.251) | 0.251 (0.208..0.276) | 1.192x |
| canvas-stacked-128 | gpu:bakeSunShadowMap | 1.416 (1.404..1.433) | 3.020 (2.914..3.092) | 2.132x |
| canvas-stacked-128 | gpu:canvasClear | 0.031 (0.030..0.031) | 0.029 (0.029..0.031) | 0.955x |
| canvas-stacked-128 | gpu:cardinalSurfaceFb | 1.920 (1.889..1.950) | 3.579 (3.437..3.656) | 1.864x |
| canvas-stacked-128 | gpu:computeLightVolume | 0.553 (0.457..0.588) | 0.110 (0.107..0.112) | 0.199x |
| canvas-stacked-128 | gpu:computeSunShadow | 0.054 (0.051..0.057) | 0.074 (0.070..0.076) | 1.362x |
| canvas-stacked-128 | gpu:computeVoxelAO | 0.137 (0.135..0.139) | 0.180 (0.174..0.188) | 1.316x |
| canvas-stacked-128 | gpu:fbToScreen | 0.122 (0.121..0.123) | 0.121 (0.114..0.125) | 0.990x |
| canvas-stacked-128 | gpu:lightingToTrixel | 0.040 (0.039..0.041) | 0.040 (0.038..0.041) | 1.000x |
| canvas-stacked-128 | gpu:trixelToFb | 1.672 (1.647..1.699) | 1.423 (1.371..1.458) | 0.851x |
| canvas-stacked-128 | gpu:voxelCardinalElect | 0.256 (0.251..0.265) | 0.402 (0.368..0.419) | 1.570x |
| canvas-stacked-128 | gpu:voxelCompact | 0.106 (0.098..0.112) | 0.120 (0.107..0.131) | 1.128x |
| canvas-stacked-128 | gpu:voxelStage1 | 0.101 (0.099..0.102) | 0.099 (0.094..0.102) | 0.982x |
| canvas-stacked-128 | gpu:voxelStage2 | 0.218 (0.215..0.220) | 0.238 (0.228..0.249) | 1.093x |
| canvas-stacked-128 | gpu_envelope_ms | 2.427 (2.387..2.457) | 4.078 (3.910..4.173) | 1.681x |
| canvas-stacked-128 | steady_mean_ms | 8.972 (8.600..9.390) | 14.606 (12.140..15.870) | 1.628x |
| canvas-stacked-128 | steady_p99_ms | 18.240 (18.020..18.470) | 21.972 (21.680..22.290) | 1.205x |
