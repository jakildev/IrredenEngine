# Lua binding surface — rationale

Why `engine/script/`'s binding surface is shaped the way it is. The rules
themselves live in [`engine/script/CLAUDE.md`](../../engine/script/CLAUDE.md);
the Lua-driven ECS design (CODEGEN vs EVAL, the DSL, the perf-parity gate) is
[`lua-driven-ecs.md`](lua-driven-ecs.md); the command/input surface is
[`lua-input-commands.md`](lua-input-commands.md). This document holds only
rationale that is still true of the code.

## Runtime and exceptions

The VM is LuaJIT 2.1 in Lua 5.1 mode. Per-archetype Lua ticks are stable inner
loops, which is the shape the trace compiler handles well; the perf story is
the "Retrospective" section of `lua-driven-ecs.md`. sol2 detects LuaJIT through
`LUAJIT_VERSION`, so bindings need no runtime-specific code.

sol2's default for LuaJIT lets a C++ exception unwind through the VM with the
platform unwinder, and the message is lost on the way: `sol::error::what()`
reports `"C++ exception"`. `SOL_EXCEPTIONS_ALWAYS_UNSAFE` makes sol2's
trampoline catch `std::exception` in-process and forward `what()` through
`lua_error`, so a `protected_function` result carries the real message. The
"unsafe" name is sol2's; here it is the safer setting. LuaJIT is built with
`LUAJIT_UNWIND_EXTERNAL` independently, so a binding that wants propagation can
flip the sol2 macro without rebuilding LuaJIT.

## Three component declaration kinds

A Lua-typed component (runtime `IRComponent.register`) is one
`IComponentDataLuaTyped` with a native vector per field, so a field read is a
typed index, never a per-frame `sol::table` lookup. A codegen'd component and a
hand-written C++ component are ordinary C++ types. The `IREntity.*Lua*` field
accessors cast to `IComponentDataLuaTyped`; unchecked, that cast on a C++-typed
impl is undefined behavior, so the accessors check and raise with a message
naming the supported path instead.

The two column views differ for the same reason: `LuaTypedColumnView` offers
`:getField`/`:setField` over named native columns, while `LuaCppColumnView`
hands back a reference to the C++ row. A tick body written with `:setField` —
the shape the codegen DSL lowers — therefore runs under CODEGEN but not when
dispatched through LuaJIT against a codegen'd struct. Unifying the two views
would restore "the same body runs under both modes" for such systems; it is a
design change, not a fix, and is not scheduled.

Attaching a C++-typed component from Lua needs a factory because the entity
core will not append a default row for one (some engine components delete
their default constructor). A codegen'd component's `bindLuaType` registers a
`default-construct → apply overrides → setComponent<T>` factory; unknown keys
and mismatched values are ignored, matching the EVAL path's row writer. A
hand-written component opts in explicitly.

In codegen coexistence the runtime `IRComponent.register` call for a name the
codegen registry already registered returns the existing handle. Registering
it again would create a parallel `IComponentDataLuaTyped` under the same name
and split archetype storage. The C++-side registration does not populate the
modifier field registry, which is why codegen'd fields have no `bindingId`.

## Packed vector fields

A packed `vec3`/`ivec3` column is byte-identical to a hand-written C++ `vec3`
field, so Lua-defined and C++ components round-trip math fields through the
same `{x, y, z}` table convention. A quaternion is a `vec4` engine-wide
(`.w` is the scalar), so `quat` and `quaternion` are tag aliases rather than a
second storage type. The `vec4` default is the identity quaternion because
rotation is the primary consumer; a plain 4-vector author passes an explicit
default. The friendly table read allocates, which is why hot paths use scalar
fields.

For a C++ component, a bare `&C_Foo::vec_` member pointer compiles but is dead
from Lua: `IRMath::vec3`/`vec4` are not registered usertypes, so Lua can
neither build a value to assign nor read the userdata it gets back. The
`sol::property` getter/setter over tables is the working shape.

## Codegen emission

**Alphabetical fields.** Lua hash iteration order is unspecified, so the tool
sorts fields by name to keep the generated header reproducible.

**Per-run registry namespaces.** Every run once emitted
`registerCodegenComponents` under one name. Distinct definitions survived only
while each stayed small enough to inline at its call site; once one was emitted
out of line, `linkonce_odr` merging kept an arbitrary winner and callers ran
another run's components. Putting each run's registry in its own namespace
makes the names distinct, so correctness no longer depends on the optimizer.
The using-directive keeps existing call sites resolving because qualified
lookup unions using-directed namespaces when the enclosing namespace declares
no such name. A keyword-shaped run id is rejected by the tool because otherwise
the error surfaces inside the generated header, pointing away from the call
that named it.

**Link-time name claims.** `IRComponents::C_X`, `bindLuaType<C_X>`, and its
attach factory are keyed by the authored component name, which namespacing
cannot separate. Each run therefore defines one external-linkage
`C_X_declared_by_more_than_one_codegen_run_in_this_binary` constant, so two runs
declaring the same component fail at link time with the cause in the symbol
name. The definition lives in the run's companion `<stem>_claims.cpp`, not the
header, so including a header from several TUs contributes one definition and
the diagnostic keeps a single meaning. `test/script/lua_component_codegen_second_tu.cpp`
locks the multi-TU case.

**Per-row tick lambdas.** The codegen tick drops the canonical outer loop and
lowers column ops to per-row references, producing the engine's default
per-component tick signature — the only one compatible with `PARALLEL_FOR`. A
body that does not fit that shape is rejected rather than lowered to a batch
form the concurrency validator would refuse. A `local a = arch.C:at(i)` that is
only read, and never read after a write to the same column, lowers to a
`const auto&` alias instead of a copy; any binding the emitter cannot prove
read-only stays by value, which is safe because the DSL forbids `a.field = v`.

**Statement bindings.** A small set of void engine setters is callable as a
bare statement so a system that computes a render parameter under CODEGEN can
also apply it, instead of stranding the application step in EVAL or a C++
bridge. Each entry names its header, emitted only when a body uses it.

**Unimplemented, by design until needed.** Codegen systems register by string
name through `IRSystem::createSystem<...>` rather than a generated `SystemName`
partial; the id lives in the same space and works with pipelines. The emitted
`kEvalSystemNames[]` is a prepared hook for a startup check that every declared
EVAL system actually registered; nothing reads it yet. `vec4` emission, `vec2`,
string formatting, and enum-member folding in tick bodies are open DSL gaps.

## EVAL concurrency

An EVAL tick is a `sol::protected_function` call into LuaJIT, and both sol2 and
LuaJIT's GC are single-threaded, so running one on a worker thread is unsound.
The runtime shim downgrades `PARALLEL_FOR` to `MAIN_THREAD` and logs once per
system so the request is visible. CODEGEN bodies are native and go through the
same template-time validator as hand-written systems.

## Hot-reload

`IRSystem.replaceSystemBody` on a Lua system reseats the captured
`sol::protected_function` inside the existing body lambda through a shared
pointer, keeping the archetype view and column-view setup intact. The C++
`IRSystem::replaceSystemBody` replaces `functionTick_` directly; both go through
`SystemManager::replaceSystemBody`. Component schemas are fixed at
registration, so re-registering a name creates a new system id rather than
migrating archetypes.

## Mode strings

The `mode` field and the component `type` tags are strings read by both the
runtime and the build-time tool, which makes them schema. They are the two
recorded deviations in [`cpp-lua-enums.md`](../../.claude/rules/cpp-lua-enums.md).
Unknown values in either layer are errors because a silent fallback hides a
typo until the missing system is noticed much later.

## Engine service bindings

**Overlap events** are keyed by layer pair and held in `DISPATCH_LUA_OVERLAP`'s
state, so a destroyed entity simply stops producing contacts and no handler
refers to a dead entity. Enter and exit are derived per pair. Registering the
system creates the `C_OverlapContactBatch` singleton, which is what switches
pair emission on in the producer.

**Sim clock** events are polled flags raised on the firing tick, matching the
events-as-components model; names are registry keys because a world has a
handful of cycles and timers.

**`IRSave`** values cross by `sol::type` inspection, not a Lua-spelled enum, so
`cpp-lua-enums` does not apply. Stores live in the per-user data directory so
they survive a bundle reinstall; the registry is captured in the binding
lambdas and dies with the `sol::state`.

**`IRPersist`** glue calls `IRWorld::saveWorld`/`loadWorld`, so Scripting links
`IrredenEngineWorld`, closing a static-library cycle with World. CMake repeats
both archives on the link line so single-pass linkers (GNU ld, mingw) rescan
World; Apple's multi-pass linker would otherwise hide a missing link. Snapshot
paths are raw because snapshots are tool artifacts. `loadWorld` does not reset
because the caller owns when the world is cleared.

**`IRDebug`** takes 0..1 colors to match the C++ overlay API argument for
argument, so overlay code ports line for line. `clear()` is not bound because
the flush owns clearing and a mid-frame clear would drop other systems' draws.
Vector arguments are type-checked per vector type before the `*FromLua` helper
runs, since the helpers default silently. A circle ring always closes: counts
that divide the cos/sin table use it and others evaluate the angle directly.

**Widgets.** A Lua `onClick` lives in `WIDGET_LUA_DISPATCH`'s session state keyed
by widget id, not in a component, and fires the frame `fireAction_` pulses — the
same lifetime contract as overlap handlers. `makePanel`'s `zOrder` is script
data, so it is clamped with a warning rather than asserted; the clamp is plain
code and holds in release builds.

**Render glue** tables are created behind a validity check so a creation's own
`IRRender` entries survive. The GUI draws rasterize through `subImage2D`, which
orders correctly against the per-frame clear in Metal's command buffer.

## Prefab spawn

The schema is versioned from the first release so editor-emitted prefabs stay
loadable; unknown top-level keys are ignored so a newer field does not break an
older loader. `rotation_mode` takes the enum integer so the authoring surface
cannot drift from the C++ enum.

Spawn order: rig and voxel attachment, then declarative `components` factories,
then `setup`, so `setup` observes and may overwrite declaratively attached
components. A non-table `components` value is treated as absent. Any failure
destroys the partial entity and spawned children.

`bind_point_overrides` merges into the rig's bind points field by field; an
unknown name adds a bind point. Spawn converts the rig's BIND chunk into
`C_BindPoints` beside `C_JointHierarchy`. `IREntity.bindPoint` composes the
joint chain root first (quaternion multiply, translation rotated by the chain)
and applies the point's local transform; without a hierarchy it returns the
local transform. A per-tick consumer should cache the resolved transform in a
component.

How a `voxel_ref` becomes components (SHAPES children, the DENSE
`C_VoxelSetNew`, headless staging) is owned by
[`engine/prefabs/irreden/voxel/CLAUDE.md`](../../engine/prefabs/irreden/voxel/CLAUDE.md)
§"Prefab.spawn voxel_ref → ECS components". A malformed dense payload yields an
empty component and no attach, so an unreadable asset is recoverable.
