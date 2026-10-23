# Guarded GRID receiver comparison

Eight native Metal Debug runs completed in a successful outer `GUARDED`
benchmark lease (exit 0). This provides a bounded cost comparison where the
earlier [surface-adoption attempts](../surface-adoption-controls/README.md)
failed quiet coverage. Battery power remains provisional by user direction:
73% before, 72% after. It is not a Release, AC-power or Windows qualification.

Two forward/reverse rounds compare surface lighting 0/1 × visibility prepass
0/1 at yaw 0.6, zoom 4, base subdivision 1 in the frozen overlapping GRID scene.
Each run records 360 frames, excludes the first 90 for steady statistics, and
captures neither screenshots nor visibility counters. All eight requested
environments, runtime identities, completed draw routes and round counts pass
the driver checks. Each report has 360/360 valid GPU frame samples and 359
completed samples per active draw stage; raster overflow drops are zero.

| Surface / prepass | Frame mean ms | Steady mean ms | GPU envelope mean ms | Surface draw mean ms |
|---|---:|---:|---:|---:|
| sampled / off | 8.755 | 8.370 | 4.206 | — |
| sampled / on (inactive control) | 8.580 | 8.355 | 4.104 | — |
| continuous / off | 10.460 | 10.035 | 5.083 | 2.201 |
| continuous / on | 9.365 | 8.935 | 5.011 | 2.711 |

These are means over two runs, not statistical guarantees. The prepass-on
continuous frame means range from 8.970 to 9.760 ms. Sampled receiving uses
separate compute lighting and scatter stages; their mean invocation durations
with prepass off are 0.104 and 0.224 ms. Draw-stage means are not additive frame
costs. GPU envelope includes submission gaps and stalls, not just GPU busy time.
Wall-frame time includes offscreen pacing and CPU/GPU scheduling.

The prepass therefore has mixed evidence: wall-frame means improve in this
window, its combined surface draw costs more, and GPU envelope means change
little. Do not choose a default from the wall-frame mean alone. The
[untimed prepass controls](../../pr-screenshots/codex/grid-receiver-prepass/README.md)
establish roughly 23% fragment rejection and identical images in this fixture;
they do not make prepass construction free. Accurate continuous receiving
still costs more than sampled receiving here. The
[polygon controls](../../pr-screenshots/codex/receiver-shadow-phase/README.md)
separately show why sampled receiving is not the correctness target.

## Retained evidence

- [run.json](run.json) records the wrapper exit, power and outer quiet verdict.
  The raw quiet report is retained as `quiet.txt.gz`; per-run `quiet: unknown`
  reflects reuse of that outer lease, whose successful verdict governs.
- [timing/cases.json](timing/cases.json) records exact arguments, arm overrides
  and order; [timing/summary.md](timing/summary.md) retains the aggregate table.
- Each round retains its manifest, summary, raw profile report and wrapper log.
  Text reports/logs are losslessly gzip-compressed. [manifest.json](manifest.json)
  hashes all measurement artifacts; [driver.py.txt](driver.py.txt) preserves the
  executed orchestration.
- All runtime fingerprints agree: binary
  `ff093ee5053bb699b6a48bbc3dba08fddf8fe85b0a0631b98a4b4d02cebe8206`, shader
  `8fa38221b8a91d26d053193df1920148d15509470d6bf0ce81d0b35d002da6bd`, scripts
  `c3ab47543e58c89b874850315b06881210a2dc81013b1524233269e9f7edad5b`.
  The [frozen source provenance](../sun-overflow-production/guarded-depth/timing/artifacts/guarded-depth/artifact.json)
  identifies the runtime. The manifest's head `169a41b7f` identifies the runner
  checkout, not that binary's source.

## Next decisions

Keep accurate receiving and prepass adoption separate. Qualify larger receiver
overdraw, different zoom/density, live camera transitions and genuinely varying
subdivision geometry. Isolate prepass build/draw cost before changing scheduling.
Retain the fog exclusion until albedo lifetime and fog composition are compatible.
No default change, shadow fix, general throughput or million-entity claim follows
from this small scene.
