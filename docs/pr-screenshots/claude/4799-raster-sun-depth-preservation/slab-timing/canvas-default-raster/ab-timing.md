| cell | metric | baseline mean (min..max) | head mean (min..max) | head / baseline |
|---|---|---|---|---|
| canvas-default-raster | cpu:BakeSunShadowMap | 0.127 (0.124..0.129) | 0.128 (0.127..0.130) | 1.005x |
| canvas-default-raster | cpu:CanvasToFramebuffer | 0.045 (0.044..0.047) | 0.044 (0.043..0.045) | 0.973x |
| canvas-default-raster | cpu:ComputeSunShadow | 0.009 (0.009..0.009) | 0.009 (0.009..0.009) | 1.000x |
| canvas-default-raster | cpu:LightingToTrixel | 0.141 (0.138..0.143) | 0.141 (0.140..0.143) | 1.001x |
| canvas-default-raster | cpu:SingleVoxelToCanvasFirst | 1.965 (1.909..1.991) | 1.963 (1.945..1.985) | 0.999x |
| canvas-default-raster | gpu:bakeSunShadowMap | 2.101 (2.030..2.164) | 2.918 (2.851..3.016) | 1.389x |
| canvas-default-raster | gpu:canvasClear | 0.012 (0.011..0.012) | 0.008 (0.008..0.008) | 0.690x |
| canvas-default-raster | gpu:cardinalSurfaceFb | 2.777 (2.683..2.872) | 3.371 (3.294..3.481) | 1.214x |
| canvas-default-raster | gpu:computeLightVolume | 0.064 (0.060..0.067) | 0.254 (0.224..0.285) | 3.975x |
| canvas-default-raster | gpu:computeSunShadow | 0.049 (0.047..0.051) | 0.040 (0.038..0.042) | 0.820x |
| canvas-default-raster | gpu:computeVoxelAO | 0.217 (0.204..0.228) | 0.261 (0.241..0.283) | 1.204x |
| canvas-default-raster | gpu:entityCanvasToFb | 3.136 (3.034..3.242) | 3.637 (3.552..3.756) | 1.160x |
| canvas-default-raster | gpu:fbToScreen | 0.049 (0.048..0.051) | 0.035 (0.034..0.036) | 0.713x |
| canvas-default-raster | gpu:lightingToTrixel | 0.030 (0.028..0.031) | 0.022 (0.022..0.023) | 0.757x |
| canvas-default-raster | gpu:shapeDepth | 0.137 (0.131..0.141) | 0.093 (0.089..0.098) | 0.682x |
| canvas-default-raster | gpu:shapeOwnerClear | 0.006 (0.006..0.007) | 0.004 (0.004..0.005) | 0.687x |
| canvas-default-raster | gpu:shapeOwnerElect | 0.134 (0.130..0.138) | 0.093 (0.089..0.099) | 0.693x |
| canvas-default-raster | gpu:shapePublish | 0.139 (0.135..0.143) | 0.096 (0.092..0.100) | 0.690x |
| canvas-default-raster | gpu:trixelToFb | 1.650 (1.586..1.711) | 2.625 (2.567..2.696) | 1.591x |
| canvas-default-raster | gpu:voxelCardinalElect | 0.018 (0.017..0.018) | 0.013 (0.013..0.014) | 0.761x |
| canvas-default-raster | gpu:voxelCompact | 0.042 (0.041..0.043) | 0.036 (0.035..0.036) | 0.848x |
| canvas-default-raster | gpu:voxelStage1 | 0.041 (0.039..0.042) | 0.030 (0.029..0.031) | 0.734x |
| canvas-default-raster | gpu:voxelStage2 | 0.019 (0.019..0.020) | 0.015 (0.015..0.015) | 0.773x |
| canvas-default-raster | gpu_envelope_ms | 7.279 (7.140..7.409) | 7.496 (7.371..7.612) | 1.030x |
| canvas-default-raster | steady_mean_ms | 23.528 (23.260..23.740) | 23.814 (23.580..24.060) | 1.012x |
| canvas-default-raster | steady_p99_ms | 28.028 (27.480..28.330) | 27.524 (26.670..28.070) | 0.982x |
