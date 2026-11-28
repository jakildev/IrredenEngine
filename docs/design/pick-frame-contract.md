# Pick frame contract

The normal render publishes a compact pick record for the cursor sampled while that frame was
rendered. INPUT consumes only completed records. If the event cursor differs from the completed
record's cursor, hover and click dispatch are deferred while the GPU re-queries that cursor
against the presented frame. No CPU texture copy or GPU completion wait occurs in INPUT.

## Normal record

Each hover-enabled `TRIXEL_TO_FRAMEBUFFER` pass contributes to the shared 32-byte
`HoveredEntityIdLayout`. Gather accepts raw-texel identity with alpha at least 0.1 and the nearest
depth. Rotating scatter resolves its `RG32UI` owner attachment only when its depth is bitwise
identical to the final framebuffer depth. After the last writer, a GPU-timeline copy publishes the
record with the rendered frame id, render-time main-framebuffer cursor pixel and submission
serial.

The CPU serves only records whose submission serial has completed. If several records complete
together, the greatest frame id wins; the newest two records remain in history for the
previous-completed-frame fault. A full eight-slot ring drops its oldest pending record. Frame
distance and call order are not completion evidence.

`IRRender::getMouseTrixelPick()` compares the current event cursor with the record's rendered
cursor. A match uses the record immediately. A mismatch is stale and queues an event-cursor query.
Entity ids are filtered after readback if they have since been destroyed or marked for deletion.

## Event-cursor query

The render system retains the presented frame's draw order, frame-data values, programs and
canvas resource identities. After INPUT and before UPDATE, a queued query begins the device frame
early, GPU-clears one compact destination record, and replays those draws in query mode. Query
mode discards every fragment except the event cursor pixel, runs the same gather rule, and discards
the colour output. The rotating attachment resolve uses the same cursor pixel before and after the
gather replay. Depth testing and depth writes are disabled, so the presented framebuffer is not
changed.

The query records its submission serial and returns control without waiting. Hover transitions
and button presses captured from that input batch remain pending until
`completedSubmissionSerial()` reaches the query. Hitbox candidates are retained with the pending
event: GUI hitboxes still have precedence, and world hitboxes still share the authored
priority/depth comparison with the queried trixel. The measured request-to-dispatch delay is
reported as `EVENT-CURSOR-PICK` evidence.

Still-cursor frames never enter query mode. Their steady path retains the existing compact record,
one 32-byte timeline copy and completion-gated ring drain; it has no framebuffer-sized pick
allocation, clear, write or copy. An event-cursor query adds GPU work only for a stale input batch.

Detached entity canvases do not contribute to the main-framebuffer trixel query. They retain
their authored world hitboxes and priority/depth arbitration in `ENTITY_HOVER_DETECT`; GUI
hitboxes retain precedence.

## One result per comparison

Each pick call drains completions again and serves the newest record completed at that call, so
two calls in one frame can see different completions: an OpenGL timestamp fence can retire between
them. `MouseTrixelPick::newestCompletedFrameId_` comes from the same history state as the served
record. A consumer comparing the served pick with the newest completed frame reads both from one
result, never from a second query.

## Backend boundary

OpenGL and Metal share the record layout, binding 14, shader gather/scatter rules, cursor Y
conversion and submission-completion gate. Metal shared storage does not make an uncommitted
command buffer visible to the CPU, and OpenGL may keep multiple submitted frames in flight.

The rejected whole-frame prototype retained one 32-byte record per framebuffer pixel. At 1282 by
722, each allocation was 29,619,360 bytes and the bounded ring plus two history images could reach
296,193,600 bytes. Five paired runs measured +0.290 ms input-to-present, +0.292 ms frame mean and
+0.271 ms GPU span, exceeding the locked bounds. The selected compact re-query removes those
steady per-pixel costs while preserving the GPU as the sole owner of pick semantics.
