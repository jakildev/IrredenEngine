# Lighting-route density cleanup evidence

Native macOS Metal, 1280×720 game resolution, 2560×1440 framebuffer. Nine
before/after full-frame pairs are byte-identical; every run exited CLEAN.
These establish unchanged output for this cleanup, not a new geometry oracle.

Source: `774d19ceede5544aaa727f6f4ae585e88d09489e`, incorporating master
`a08e0a44ad3df5839249086dc08d6615c6006423` and the parent dispatch cleanup.
The **before** build applies [baseline.patch](baseline.patch), restoring only
the two original `LightingRouteScope` density writes. The **after** build uses
the committed source without that patch. Both use the same diagnostic helper
and capture controls. [captures.json](captures.json) records commands, source
revision, binary hashes, camera/probe lines and every image hash. The `.log`
files retain capture/probe and clean-exit lines.

| Scene | Captures | Observed coverage |
|---|---|---|
| Overflow fog | rotated, cardinal, resumed | effective density 16, store cap 8; 39 overflow entries on both rotated captures; cardinal parks axes, resumed rotation has live axes |
| FIELD fog LOS | rotated, cardinal, resumed | effective density 16, store cap 8; 32 overflow entries; high-ground occlusion fixture with LOS |
| Mixed canvas stress | negative, cardinal, positive yaw | subdivisions 16; main GRID and detached canvases, AO and sun shadows |

Commands, after building `IRFogDemo` and `IRCanvasStress`:

```text
fleet-run IRFogDemo --auto-screenshot 10 --lighting-density-check --peraxis-overflow
fleet-run IRFogDemo --auto-screenshot 10 --lighting-density-check --occlusion=high-ground
fleet-run IRCanvasStress --auto-screenshot 10 --no-spin --no-auto-rotate --pivot-origin --zoom 4 --subdivisions 8 --sweep-yaw -0.35 0.35 3
```

Fog poses use zoom 4 and yaw 0.35 → 0 → 0.35 radians. The overflow fixture's
magenta is intentional fog debug color. Fog registration forces compute
lighting, covering the scope's lighting and fog consumers. The diagnostic
readback blocks on GPU completion; these runs must not be used as benchmarks.

Representative full frames were visually inspected for context. Exact image
identity preserves inherited face/shadow patterns; it does not resolve them.
Native OpenGL was not exercised on this host. The shader audit checked both
backends, including the merged smooth-yaw AO specialization and fog-channel
changes. No blur, shadow tuning or density-dependent raster change is included.

## Broad verification and inherited reference failures

The 71 Python render-harness suites passed. Header/binding checks passed, and
both demos built on Metal. Full native verification passed 28/30 CanvasStress
checks and 54/55 FogDemo checks. The remaining reference comparisons are:

| Shot | Maximum channel delta | Pixels above the limit of 64 |
|---|---:|---:|
| `so3_offsnap_disc` | 73 | 16 |
| `so3_offsnap_wide` | 73 | 16 |
| `fog_explored_decay_tiers_yaw` | 138 | 24 |

The CanvasStress outliers form one 4×4 floor-corner block per shot. The fog
outliers form three 4×2 blocks on a white panel's edges. Whether these are
intended receiver/face corrections or defects remains a separate geometry
investigation; neither references nor thresholds were changed to pass them.

Attribution replaced every modified engine file and the fog demo source with
master `a08e0a44a`, using [master-renderer.patch](attribution/master-renderer.patch)
against the capture revision. The two new, unreferenced dispatch-helper files
remained on disk. All 12 default CanvasStress frames and all five explored-decay
frames were byte-identical to the cleanup candidate, including the three
failing shots. Both master runs exited CLEAN. Thus these failures are present
in that merged master renderer and are not introduced by either cleanup PR.

[Attribution hashes and comparisons](attribution/captures.json) cover all 17
frames. The three failing full-frame pairs are retained in `attribution/`.
The committed cleanup sources were restored after the experiment.

Follow-up hypotheses, not correctness verdicts: the floor brightening is
consistent with the merged finite-box silhouette-miss recovery. Compare strict
and recovered ray/slab hits and normals at the two affected samples, with
shadow and no-AO controls. The fog change is exactly the explored-state gray
mapping (`230 × 0.4 = 92`); inspect the selected carrier, decoded surface and
rounded world column against the authored visible-disc radius before attributing
it to lighting or refreshing its reference.
