# Pick frame contract

The GPU hover buffer describes the trixel under the cursor sampled for one rendered frame. It is
not an answer for the cursor at the time an INPUT handler later asks for a pick.

## Record

Each `TRIXEL_TO_FRAMEBUFFER` pass publishes a GPU-timeline copy after its final hover writer. The
copy is tagged with:

- the rendered frame id;
- the main-framebuffer cursor pixel used by that frame; and
- the submission serial that contains the copy.

The CPU serves only slots whose submission serial has completed. If several slots become readable
together, the record with the greatest frame id wins. A full ring drops its oldest pending slot;
the read path never waits for room and never maps the live buffer a queued submission can still
write.

## INPUT result

`IRRender::getMouseTrixelPick()` returns the most recent completed record. Its entity is cleared if
that id has since been destroyed or marked for deletion. Entity ids are monotonic, so a surviving
record cannot alias a later entity.

Each call serves the newest record completed at that call, so two calls in one frame can see
different completions: on OpenGL a submission can retire between them. The result therefore carries
`newestCompletedFrameId_`, the newest completed record's frame id in the same history state the
served record came from. A consumer comparing the served pick with the newest completed frame reads
both from one result, never from a second query.

The result is stale when its recorded cursor and the current INPUT cursor occupy different
main-framebuffer pixels. Staleness is reported without suppressing the entity: hover and click
dispatch retain their existing behaviour while the profile report counts stale INPUT ticks once
per tick, regardless of how many consumers ask for the pick.

## Backend boundary

OpenGL and Metal share this contract through `submissionSerial()` and
`completedSubmissionSerial()`. Frame distance and call order are not completion evidence. Metal
shared storage does not make an uncommitted command buffer visible to the CPU, and OpenGL may keep
multiple submitted frames in flight.

The 32-byte `HoveredEntityIdLayout`, its binding, GPU writers, per-frame reset, and visible hover
highlight are unchanged.
