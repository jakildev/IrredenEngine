Process deltas use the pre-scene resident sample from the same run. Metal reports process-owned GPU allocation. OpenGL reports NVIDIA device capacity and availability when GL_NVX_gpu_memory_info is exposed; device used is not process residency.

| Case | Runs | Bake mean us | Asset bytes | Scratch peak bytes | Bake payload peak bytes | RSS after bake delta bytes | Final RSS delta bytes | Process peak delta bytes | GPU process bytes | GPU device used bytes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| density-direct-1-timing-off | 3 | 0.0 | 0.0 | 0.0 | 0.0 | 8192.0 | 34039125.3 | 35102720.0 | 0.0 | 1950475605.3 |
| density-baked-1-timing-off | 3 | 22.7 | 63888.0 | 5324.0 | 69212.0 | 20480.0 | 29360128.0 | 30664021.3 | 0.0 | 1941147648.0 |
| density-baked-2-timing-off | 3 | 116.0 | 444528.0 | 37044.0 | 481572.0 | 20480.0 | 30677674.7 | 31462741.3 | 0.0 | 1943396352.0 |
| population-direct-16-overlap0-timing-off | 3 | 0.0 | 0.0 | 0.0 | 0.0 | 8192.0 | 37369173.3 | 37967189.3 | 0.0 | 1958550186.7 |
| population-direct-16-overlap1-timing-off | 3 | 0.0 | 0.0 | 0.0 | 0.0 | 8192.0 | 41354581.3 | 41842005.3 | 0.0 | 1961848832.0 |
| population-baked-16-overlap0-timing-off | 3 | 25.0 | 63888.0 | 5324.0 | 69212.0 | 20480.0 | 35558741.3 | 36020224.0 | 0.0 | 1952064853.3 |
| population-baked-16-overlap1-timing-off | 3 | 22.7 | 63888.0 | 5324.0 | 69212.0 | 20480.0 | 39856810.7 | 41142954.7 | 0.0 | 1939350869.3 |
| motion-direct-root-timing-off | 3 | 0.0 | 0.0 | 0.0 | 0.0 | 8192.0 | 34515626.7 | 34754560.0 | 0.0 | 1950710442.7 |
| motion-direct-parented-timing-off | 3 | 0.0 | 0.0 | 0.0 | 0.0 | 8192.0 | 34489685.3 | 34895189.3 | 0.0 | 1967443968.0 |
| motion-baked-root-timing-off | 3 | 27.7 | 63888.0 | 5324.0 | 69212.0 | 20480.0 | 30741845.3 | 32197290.7 | 0.0 | 1955800405.3 |
| motion-baked-parented-timing-off | 3 | 23.3 | 63888.0 | 5324.0 | 69212.0 | 20480.0 | 29896704.0 | 31053141.3 | 0.0 | 1967596885.3 |
