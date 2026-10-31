# SDF occupancy memory qualification — AC Metal Release

This bounded slice qualifies the memory instrumentation and records density,
population and motion observations for issue #4762. It does not select a global
representation default. All 33 runs were clean, guarded, on AC power, and used
the same Release binary and shader/runtime fingerprints on macOS/Metal. Each of
the 11 cases ran three times for 300 measured frames with stage timing disabled.

Process columns are deltas from a sample taken immediately before the fixture
scene is created, after engine and renderer initialization. `RSS final` is the
post-profile resident delta; `RSS peak` uses the operating system's process
high-water mark. Asset and scratch sizes are exact baker payload accounting,
not allocator overhead. The Metal value is `currentAllocatedSize()`, which is
driver memory currently attributed to this process.

| Case | Runs | Bake µs | Asset KiB | Scratch KiB | RSS final MiB | RSS peak MiB | Metal allocation MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Density baked 1 | 3 | 20 | 62.4 | 5.2 | 138.71 | 138.88 | 449.92 |
| Density baked 2 | 3 | 112 | 434.1 | 36.2 | 141.02 | 144.10 | 449.92 |
| Density direct 1 | 3 | 0 | 0 | 0 | 143.81 | 143.81 | 457.70 |
| Motion baked, parented | 3 | 19 | 62.4 | 5.2 | 138.90 | 138.91 | 448.59 |
| Motion baked, root | 3 | 20 | 62.4 | 5.2 | 140.74 | 140.89 | 447.26 |
| Motion direct, parented | 3 | 0 | 0 | 0 | 145.06 | 145.06 | 457.73 |
| Motion direct, root | 3 | 0 | 0 | 0 | 143.74 | 143.76 | 455.07 |
| Population baked 16, spaced | 3 | 22 | 62.4 | 5.2 | 150.44 | 158.02 | 449.92 |
| Population baked 16, overlap | 3 | 20 | 62.4 | 5.2 | 150.90 | 153.40 | 449.92 |
| Population direct 16, spaced | 3 | 0 | 0 | 0 | 149.24 | 151.19 | 457.80 |
| Population direct 16, overlap | 3 | 0 | 0 | 0 | 151.06 | 152.56 | 458.17 |

Density two raises exact baked asset plus peak scratch from about 68 KiB to
470 KiB, while mean peak process RSS rises by about 5.2 MiB relative to density
one. That difference is why exact payload and process residency remain separate
measurements. Sixteen-copy cases raise process residency without raising the
Metal allocation witness, consistent with reuse inside already allocated GPU
pools. Direct cases observe roughly 5–11 MiB more Metal allocation in this
fixture, but these process snapshots are not a general throughput or memory-win
claim.

The runner requires exactly one `SDF-OCCUPANCY` and one `SDF-RESIDENCY` witness
per accepted run and writes their distinct fields to each manifest and
`memory-summary.md`. The matrix audit found 33/33 fresh reports, GPU
measurements, stable configurations and exit code zero, with no missing
witnesses. The binary SHA-256 was
`2a8b52fff1fe03f205d7c81cc9ba2ace94024cf69bbd7b8e661d6326dae4a5b0`.

Windows/OpenGL remains open. When `GL_NVX_gpu_memory_info` is available, the
same API records dedicated device capacity and current device-wide availability;
those counters are not process-owned residency and must be compared under their
own label on the ship host.
