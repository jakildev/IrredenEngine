# Pick frame contract

The GPU produces a pick image for each rendered main framebuffer. INPUT addresses the newest
completed image at its event cursor, using that image's presentation mapping. Moving and pressing
in one input batch therefore selects the visible owner at the new cursor without waiting for
another GPU submission.

## Record

Each `TRIXEL_TO_FRAMEBUFFER` pass claims a retained-ring slot, clears it on the GPU and writes its
pick image during the existing composite. The slot is tagged
with:

- the rendered frame id;
- the main-framebuffer cursor pixel used by that frame;
- window-point scale, framebuffer translation, output scale and framebuffer dimensions; and
- the submission serial that contains the writes.

The CPU serves only slots whose submission serial has completed. If several slots become readable
together, the record with the greatest frame id wins. The two newest completed images leave the
writable ring and remain pinned in history; eviction returns their buffers to the ring. A full
ring drops its oldest pending slot. GPU writes remain ordered, and the read path never waits for
room or reads the live buffer a queued submission can still write.

The buffer contains a 32-byte `HoveredEntityIdLayout` prefix for the rendered cursor, followed by
one record per framebuffer pixel in raster order. The prefix preserves the existing shader hover
highlight. Gather uses the same GPU helper for the prefix and image records: raw texel identity,
alpha at least 0.1, non-null identity and depth less than or equal to the stored pick depth.
Scatter retains its RG32UI attachment and depth-tested writer. Its resolve captures each pixel's
scatter owner and depth, then accepts that owner only when the final framebuffer depth is bitwise
identical. These are GPU rules; the CPU performs address mapping and entity-lifetime filtering.

## INPUT result

`IRRender::getMouseTrixelPick()` maps the INPUT cursor through the completed record's mapping and
reads one 32-byte record. A cursor outside the image returns no entity without reading a pixel.
The framebuffer's raster Y is inverted from the top-origin window cursor on both backends. The
result's entity is cleared if that id has since been destroyed or marked for deletion. Entity ids
are monotonic, so a surviving record cannot alias a later entity.

The result records the addressed pixel and is not cursor-stale. The profile's stale INPUT tick
counter remains a regression witness. The previous-completed-frame fault switch still selects
the older image, so frame identity assertions remain independent of cursor correctness.

Detached entity canvases do not write the main-framebuffer pick image. They retain their authored
world hitboxes and priority/depth arbitration in `ENTITY_HOVER_DETECT`; GUI hitboxes retain their
existing precedence. This change does not turn detached canvas texels into voxel picks.

## Backend boundary

OpenGL and Metal share this contract through `submissionSerial()` and
`completedSubmissionSerial()`. Frame distance and call order are not completion evidence. Metal
shared storage does not make an uncommitted command buffer visible to the CPU, and OpenGL may keep
multiple submitted frames in flight.

The 32-byte record layout and binding 14 remain shared by both backends. The live allocation and
each retained allocation occupy `32 * (1 + width * height)` bytes. History retains two completed
images in addition to pending ring slots. No full-image GPU or CPU copy is required. The
additional GPU clear, per-pixel writes and scatter resolves require measured performance
qualification; functional correctness alone does not qualify this design.

## Prototype qualification

This implementation is rejected as the selected design by criteria 4 and 5. The
[measurement record](../perf/event-cursor-pick/README.md) distinguishes passing functional checks
from the locked performance failures. At a 1282 by 722 framebuffer, each allocation is
29,619,360 bytes. The bounded eight-slot ring plus two retained history images can reach
296,193,600 bytes at that size. This is an allocation calculation, not a GPU timing measurement.

The full-frame buffer preserves the composite's existing GPU hover rule and avoids adding an
attachment to every gather pipeline. It has a larger record than a compact ID-plus-depth image,
because each pixel retains the scatter owner's depth witness. The existing RG32UI scatter
attachment remains the sole rotating geometry ID writer. The retained data proposal is recorded
on [the rotating-owner issue](https://github.com/jakildev/IrredenEngine/issues/4820#issuecomment-6943352858).

Copying canvas textures and evaluating their hover rule on the CPU is excluded by the task's
single-owner constraint. A one-pixel GPU re-query remains a viable alternative to measure; it
needs retained sources and deferred dispatch. Its dispatch delay remains unmeasured. The direct
retained-ring prototype adds no dispatch queue or explicit GPU wait, but five paired runs still
measure +0.290 ms mean input-to-present, +0.292 ms frame mean and +0.271 ms GPU span. Those exceed
the locked 0.118 ms latency and 0.160 ms frame bounds, so design direction is required before a
different mechanism is implemented.
