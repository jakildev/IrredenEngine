# One append per overflow face

Overflow mode 3 admits only the invocation matching `faceOffset_2x3(slot, 0)`.
Both trixel lanes previously appended identical full-face records. The existing
layout selects X `(1,1)`, Y `(0,1)` and Z `(0,0)`, retaining every face while
avoiding the duplicate append. Other store/election modes and cardinal trixel
emission are unchanged. Fog selection and riser flips precede the lane guard
and are identical within each pair.

The [earlier rejection](per-axis-single-face-writer.md) remains recorded. Its
12-pixel seam is now independently reproduced and repaired by
[frozen coverage arbitration](../design/frozen-scatter-flicker.md); this retry
compares against that repaired parent rather than accepting image tolerance.

## Fresh validation

- IRCanvasStress frozen pose 0.47: all nine full RGB frames match the repaired
  parent across yaw 0 through 2π (1156–1164 versus 1185–1193).
- Twenty constant-yaw-45 captures (1194–1213) all match the stable parent image
  byte for byte. No flicker at the previously unstable face junction.
- IRFogDemo `--edge-yaw-sweep --auto-screenshot 6`: all 24 full frames match.
  Candidate 73–96 was captured first; removing only the lane guard gives the
  parent control 97–120. The guard was then restored and assets rebuilt.
- Native builds for CanvasStress, PerfGrid and FogDemo, header/Metal registry
  checks and comment lint pass. Focused reviewer found no missing lane or unique
  payload. OpenGL runtime remains untested; shader twins use the same guard.

Representative paired screenshots are in
`docs/pr-screenshots/codex/overflow-face-dedup/`.

## Performance

Apple M4 Max, native Metal Debug, frozen IRPerfGrid 64³, yaw 45°, zoom 4,
three 600-frame runs per version, profiler enabled, CPU sampling disabled.
Parent reports are in [frozen-scatter-flicker/](frozen-scatter-flicker/); candidate
reports and provenance in [overflow-face-dedup/](overflow-face-dedup/).

| Measurement | Parent mean (range) ms | Candidate mean (range) ms |
|---|---:|---:|
| Frame | 19.370 (19.240–19.470) | 18.923 (18.910–18.930) |
| GPU perAxisScatter | 3.000 (2.995–3.006) | 2.519 (2.511–2.527) |

Frame time falls about 2.3%, scatter about 16.0% in this local grouped-run
comparison. Sampled GPU invocation times are not additive frame totals.
The parent reports 371,422 dropped overflow appends at capacity 524,288;
the candidate logs no overflow-drop warning. Below capacity the record count
halves; at saturation the same capacity can retain more distinct faces, so
saturated-scene pixel equality is not a correctness requirement. This is not
rotation parity or a cross-hardware performance claim.
