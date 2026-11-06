# Native Metal rotation-picking acceptance (2026-11-04)

[The acceptance receipt](acceptance-2026-11-04.txt) records the checks rerun on
`4e8e10656c23e8c854fd6650cbef9c6205b28fea`, after the corrected implementation
merged in PR #4864. The eight-shot session, 360-degree sweep, three identical
tie runs and 288-sample cube probe pass. Removing only the retained-ID binding
makes the tie probe fail with 5,090 disallowed reads, proving that the hidden
private canvas exercises the binding lifetime.

The macOS display connection worked in this session. The resumed performance
pass reused the configured build tree: it built the implementation's first
parent (`45cf005a8`), copied that complete tree as the frozen base arm, then
built current master (`463814f2a`) as the head arm. Five interleaved pairs at
yaw 0.35 used 300-frame windows with GPU stage timing enabled. Frame means are
18.662 ms base versus 18.906 ms head (1.013x); the three scoped GPU rows are
all lower at head mean. [The comparison](performance-comparison.json) retains
the paired inputs, base spreads, ratios and commands. The earlier denied
manual configuration remains recorded in
[the blocked receipt](blocked-base-configure.json), but no fresh configure or
package-registry write was needed for the completed measurement.

The full ShapeDebug render verifier fails 19 of 148 checks (ten extra runs
skip for missing macOS references), and CanvasStress fails 21 of 59 checks.
Current master reproduces the earlier report row for row, including the
ShapeDebug maximum channel deltas of 109–120 and every CanvasStress metric.
The PR therefore moves no render-verifier row. No reference was re-blessed;
the inherited macOS reference drift and ten missing references are tracked in
#4904. The original tables remain in
[the render report](render-verify-2026-11-04.txt). OpenGL runtime verification
remains tracked in #4865.

The original fixture-only controls, captures and amendment receipts retain
their provenance in the [historical evidence](README.md).
