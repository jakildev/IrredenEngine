# Guarded vertex depth versus forced fragment depth

Eight paired native Metal cases completed with 16 CLEAN captures. Every pair
has zero changed screenshot RGB pixels and an equal final printed depth sample
at main-framebuffer texel `(640,360)`. Cases cover a source box touching and
intersecting a GRID floor, plus source boxes intersecting another source box at
its upper and side surfaces, at camera yaw `0` and `0.6`.

[captures.json](captures.json) preserves the driver's original commands,
environment, comparisons and artifact fingerprints. [manifest.json](manifest.json)
maps original capture paths to archived files and records their hashes. The 16
screenshots reference eight unique PNGs under `images/`; the zero-valued RGB
difference images share a ninth PNG. Each capture retains its original config,
compressed raw run log, compressed depth-sample log and depth-sample JSON.
Configuration and command paths intentionally retain the original `/tmp` paths.

The initial `(321,361)` probe sampled background and the driver rejected that
setup. It contributes no passing capture to these results. The final run used
the empirically verified non-background texel `(640,360)`. Canvas trixel extent
is not the composite framebuffer extent.

## Artifact reconstruction

Both variants use native executable SHA-256
`ff093ee5053bb699b6a48bbc3dba08fddf8fe85b0a0631b98a4b4d02cebe8206`.
Their [guarded manifest](artifacts/guarded-depth/artifact.json) and
[fallback manifest](artifacts/forced-fallback/artifact.json) retain hashes for all
319 frozen runtime entries. The archived CMake caches retain build settings;
native binaries and unchanged runtime assets are not duplicated here.

The exact source overlay for each variant is retained under its `source/`
directory, with a hash manifest. Starting from commit
`d832ed2ec5de4813e77a309bed0e1ab43719a346`, apply that directory's `engine.patch`
and copy its `engine/` tree over the checkout. The patch supplies tracked
changes; the tree supplies the eight shader files that were untracked when the
artifact was frozen. This reconstructs source content, not a claim that a fresh
compiler invocation produces the identical binary.

The runtime fallback control uses the same executable and assets, with only the
GLSL and Metal `ir_source_face_depth` helpers changed. Their final eligibility
return is replaced with `false`, so both specialized vertex routes read the same
forced-fallback decision. The exact original and modified runtime helpers are
under each variant's `runtime-shaders/`. The
[overlay provenance](artifacts/forced-fallback/overlay-provenance.json) records
the exact replacement and original/modified hashes. Applying these two modified
helpers to a copy of the guarded runtime artifact recreates the control without
a native rebuild.

The [archived driver](shadow-depth-rounding-controls.py) is the exact final-run
driver, SHA-256
`7255f762655a7b0e9fbf3e2468202176cb1f3a129d52075cdace50745d687467`.
Its default invocation prints a plan; `--run` launches through `fleet-run` with
`IRREDEN_BUILD_DIR`. Reuse requires reconstructing its recorded artifact paths
and selecting an available output directory. [archive-files.json](archive-files.json)
hashes the retained files.

## Evidence limits

The image comparison covers the complete captured RGB arrays. The depth probe
reads one real framebuffer texel each frame but prints only six decimal places
of normalized depth and one decimal place of raw distance. The comparison uses
the final printed sample; all per-frame samples remain available. Matching
printed values do not establish bitwise depth equality across the framebuffer,
and decoded tier/face values do not identify the winning source entity.

These ordinary-priority fixtures do not provide native coverage of foreground
priority, clamp transitions, other subdivision settings or Windows/OpenGL.
Per-frame depth readback flushes the GPU, so these captures establish no timing
result. The result supports these eight Metal visual controls only.
