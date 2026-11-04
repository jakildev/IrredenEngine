# Native Metal rotation-picking acceptance (2026-11-04)

[The acceptance receipt](acceptance-2026-11-04.txt) records the checks rerun on
`4e8e10656c23e8c854fd6650cbef9c6205b28fea`, after the corrected implementation
merged in PR #4864. The eight-shot session, 360-degree sweep, three identical
tie runs and 288-sample cube probe pass. Removing only the retained-ID binding
makes the tie probe fail with 5,090 disallowed reads, proving that the hidden
private canvas exercises the binding lifetime.

The macOS display connection worked in this session. Performance acceptance
remains blocked: configuring the archived pre-implementation source attempted
an out-of-sandbox RtAudio package-registry write and received `Operation not
permitted`. [The exact command](blocked-base-configure.json) records its argv.
CMake returned zero with a warning, but the denied operation was not retried
and no baseline build or A/B measurements followed. The manual configuration
attempt also violated the fleet's configured-tree build rule; a resumed run
must use a preconfigured baseline tree. This evidence does not close #4820.

The full ShapeDebug render verifier also fails 19 of 148 checks (ten extra
runs skipped for missing macOS references). Failures are 180/270-degree rows
with maximum channel differences of 109–120 against the limit of 64; the full
table is in [the render report](render-verify-2026-11-04.txt). CanvasStress also
fails 21 of 59 checks; its table is in the same report. No references were
re-blessed and no claim is made that criterion 6 passes. OpenGL runtime
verification remains tracked in #4865.

The original fixture-only controls, captures and amendment receipts retain
their provenance in the [historical evidence](README.md).
