Process deltas use the pre-scene resident sample from the same run. Metal reports process-owned GPU allocation. OpenGL reports NVIDIA device capacity and availability when GL_NVX_gpu_memory_info is exposed; device used is not process residency.

| Case | Runs | Bake mean us | Asset bytes | Scratch peak bytes | Bake payload peak bytes | RSS after bake delta bytes | Final RSS delta bytes | Process peak delta bytes | GPU process bytes | GPU device used bytes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| density-direct-1-timing-on | 3 | 0.0 | 0.0 | 0.0 | 0.0 | 8192.0 | 32875861.3 | 33390592.0 | 0.0 | 1962980693.3 |
| density-baked-1-timing-on | 3 | 26.0 | 63888.0 | 5324.0 | 69212.0 | 20480.0 | 27799552.0 | 28812629.3 | 0.0 | 1952626005.3 |
| density-baked-2-timing-on | 3 | 111.7 | 444528.0 | 37044.0 | 481572.0 | 24576.0 | 33811114.7 | 34198869.3 | 0.0 | 1952626005.3 |
| population-direct-16-overlap0-timing-on | 3 | 0.0 | 0.0 | 0.0 | 0.0 | 8192.0 | 39661568.0 | 39910058.7 | 0.0 | 1967797589.3 |
| population-direct-16-overlap1-timing-on | 3 | 0.0 | 0.0 | 0.0 | 0.0 | 8192.0 | 43743914.7 | 43874986.7 | 0.0 | 1973215232.0 |
| population-baked-16-overlap0-timing-on | 3 | 26.0 | 63888.0 | 5324.0 | 69212.0 | 20480.0 | 39103146.7 | 39852714.7 | 0.0 | 1962744490.7 |
| population-baked-16-overlap1-timing-on | 3 | 22.7 | 63888.0 | 5324.0 | 69212.0 | 20480.0 | 42287104.0 | 42988885.3 | 0.0 | 1967768917.3 |
| motion-direct-root-timing-on | 3 | 0.0 | 0.0 | 0.0 | 0.0 | 8192.0 | 35398997.3 | 35403093.3 | 0.0 | 1985748992.0 |
| motion-direct-parented-timing-on | 3 | 0.0 | 0.0 | 0.0 | 0.0 | 8192.0 | 33453397.3 | 33465685.3 | 0.0 | 1979346944.0 |
| motion-baked-root-timing-on | 3 | 26.7 | 63888.0 | 5324.0 | 69212.0 | 20480.0 | 28136789.3 | 28616021.3 | 0.0 | 1960559957.3 |
| motion-baked-parented-timing-on | 3 | 26.3 | 63888.0 | 5324.0 | 69212.0 | 20480.0 | 27742208.0 | 28547754.7 | 0.0 | 1963399850.7 |
