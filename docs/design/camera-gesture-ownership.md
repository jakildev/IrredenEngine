# Camera gesture ownership for raw-input tools

Camera drag systems run in RENDER, after authoring tools often consume raw
pointer input in INPUT. Command capture alone does not prevent both consumers
from acting on one Alt+left or Space+left gesture.

A raw-input tool owns an `IRPrefab::Camera::GestureOwnership` member from
`irreden/render/camera_gesture.hpp`. Call `poll()` once per INPUT tick, after
`INPUT_KEY_MOUSE`, including while the tool is disabled. A true result means the
camera owns this input: settle an existing edit and suppress new edits and hover
previews. `GIZMO_DRAG` uses this contract automatically. It finishes its current
drag at the last applied transform; the authoring owner retains undo policy.

The query checks which standard camera drag systems were registered. With no
matching camera control, Alt/Space clicks remain available to the tool. Chords
match the camera systems: Alt+left rotates; Space+left pans only without Alt;
Ctrl+middle rotates; unmodified middle pans. A registered navigation modifier blocks edits and hover before a pointer press
and interrupts a held edit. Camera ownership is latched at press and persists through button release even when the modifier releases first. Combined
press/release events are owned for that tick. A camera gesture that interrupts
an existing left edit suppresses its remainder, including its release.

A creation with additional navigation bindings passes their active input as
`poll(extraCameraInput)`. It owns that binding list: the shared helper never
assumes right-drag or letter-key navigation. Custom editor tools should reuse
this helper rather than inspect camera systems' late RENDER `dragging_` flags.
To protect gizmos from those same custom bindings, call
`System<GIZMO_DRAG>::queueCameraInput(active)` before its INPUT `beginTick`.
The system consumes the queued claim once; its gesture owner retains the
left-button remainder after the custom binding releases.

This is a tool-local ownership query, not a global input router. It does not
capture GUI widgets, change camera bindings, or decide whether an interrupted
edit is committed or cancelled. Those policies remain with their owners.
