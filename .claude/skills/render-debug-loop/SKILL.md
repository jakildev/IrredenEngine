---
name: render-debug-loop
description: >-
  Investigate and validate rendering defects with controlled captures, visual
  and numerical evidence, and targeted fixes. Use for an isolated visual bug
  or a continuing rendering audit across geometry, lighting and backends.
---

# Render Debug Loop

## Scope and progress

For a single defect, identify the symptom and a falsifiable acceptance criterion.
For an ongoing audit, maintain an ordered TODO in the existing task or design
artifact: distinguish diagnosed, experimentally tested, implemented and validated
work. Preserve the user’s priorities when one symptom reveals another. A passing
local experiment or an opened PR completes that slice, not the broader assignment.
Use the existing commit-and-push workflow for reviewable slices; preserve the
session’s authorization and draft/ready preference.

An **experiment** tests a stated hypothesis by changing a controlled input or
implementation and evaluating the resulting evidence. A multi-shot sweep is one
experiment, not one iteration per screenshot. Unchanged rebuilds and verification
reruns do not create new hypotheses.

There is no fixed iteration cap. After several attempts on the same symptom with
no new evidence, reassess the hypothesis, instrumentation or abstraction level;
do not keep varying shader constants blindly. Capture a smaller fixture, inspect
an intermediate buffer, derive the coordinate transform, or move to independent
authorized work while recording the unresolved item. Reassessment is not a reason
to end the session or ask for renewed permission. Stop dependent work when it
requires missing user input, unavailable capability or an explicit user limit;
state that concrete limitation and continue other authorized work where possible.

## Investigation

1. Establish the affected render path and coordinate/data contract. Trace the
   producer, intermediates and consumer; a visible lighting defect may originate
   in geometry or depth reconstruction. Treat topic diagnoses as hypotheses, not
   proof, and read the applicable module instructions before edits.
2. Choose a discriminating control: a known primitive, isolated pass, paired
   rendering path or analytical projection. Establish the failure from an adequate existing capture or capture it before
   changing it. If the demo cannot distinguish the hypotheses, extend its probes,
   controls or metrics first.
3. Run the smallest useful experiment using
   [capture and evaluation](references/capture-and-evaluation.md). Vary one cause
   at a time where possible. Record combined changes when they are necessary, so
   their effects are not misattributed.
4. Decide from evidence: retain and validate a fix, reject an experiment, or refine
   the hypothesis. Restore rejected edits before the next independent experiment;
   preserve a patch when the result will be cited. Report an unresolved finding
   honestly rather than presenting improved screenshots as a complete fix.
5. Validate the affected contracts and adjacent modes before publishing. Explain
   remaining limitations and continue the ordered TODO within the user’s scope.

## Topic references

Load only the relevant diagnosis; follow evidence across topics when needed.

| Topic | Reference |
|---|---|
| Capture setup, baselines, image metrics and temporal/scale checks | [Capture and evaluation](references/capture-and-evaluation.md) |
| Trixel/SDF geometry, silhouettes, parity and depth ordering | [Shapes](diagnosis/shapes-trixel-sdf.md) |
| AO, sun shadows, light volumes and fog | [Lighting](diagnosis/lighting.md) |
| OpenGL/Metal differences | [Backend parity](diagnosis/backend-parity.md) |
