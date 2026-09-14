# World snapshot persistence — rationale

The rules a serializer author follows live in
[`engine/world/CLAUDE.md`](../../engine/world/CLAUDE.md) §"World snapshot
(`IRWS`)". This document keeps the reasoning behind them; the mechanism (file
layout, projection walk, load phases, version dispatch) is documented on the
headers under `engine/world/include/irreden/world/` and is not repeated here.
The on-disk format contract is `engine/asset/CLAUDE.md` §"Save format
extensibility rules".

## Two save layers, no overlap

`chunk_persistence.hpp` saves a streaming chunk's **voxel slice** as one
`.vxs` file per chunk. `world_snapshot.hpp` saves **entities and
components** as one `IRWS` file. Neither reads the other's output: a chunk
file carries no component data, and a snapshot carries no per-chunk voxel
slice. Keep it that way — a component that embeds voxel records
(`C_VoxelSetNew`) serializes its own authored data through `SaveSerialize`,
never through the chunk path.

## Why the primary `SaveTrait` is "no decision yet"

A component with no `IR_SAVE_OPT_IN` / `IR_SAVE_OPT_OUT` line is a build
error, not an opt-out. Persistence is the one subsystem where a silently
skipped component looks correct in every test that does not reload a file:
the save succeeds, the load succeeds, and the missing state surfaces as a
gameplay bug with the culprit long gone. The compile-time gate
(`save_component_inventory.hpp`'s `static_assert` over `AllEngineComponents`)
plus the header-declaration comparison
(`cmake/run_save_inventory_population_check.cmake`) turn that class of bug
into a build failure at the commit that introduced the component.

The same reasoning fixes the process-default registry's membership: it is
derived from `AllEngineComponents`, so there is no second list to forget.
Every opted-in entry instantiates `SaveSerialize<C>`, which makes "opted in
but no serializer" a build error too. A per-component `register` line would
reintroduce exactly the bookkeeping this design removes.

## Why a reader is tighter than the constructor

`SaveSerialize<C>::read` admits bytes into a live component that the rest of
the engine then indexes without checks. A well-formed-but-inconsistent file
(a count above an inline array's capacity, a stored extent that disagrees
with the parallel grid it sizes) is not caught by the short-read path, so the
reader re-checks every invariant an accessor relies on and returns
`BinaryIOError::UnknownTag` on violation. Three consequences:

- **Match the accessors, not the constructors.** Where a constructor is laxer
  than the indexing math its own accessors perform (a both-negative extent
  multiplies to a positive cell count), the reader is deliberately the tighter
  of the two. A reader that only mirrored the constructor would restore a
  component the accessors then index out of bounds.
- **Constrain each dimension, never a derived product.** A reduction (a
  product, a sum) destroys sign and magnitude information, so a guard on the
  product accepts extents the per-axis guard rejects.
- **One fault, one check, one message.** A load-time diagnostic is the only
  thing the person debugging a corrupt save has; a shared message
  mis-describes whichever fault it was not written for.

When a guard is tighter than the type — the rejected state is one the
component's own API can produce — the producer is the bug, and `read` reports
it one save/load cycle late with the offending entity gone. A debug-only
`IR_ASSERT` mirror of the same guard in `write` catches the producer while
the entity is live; `read` stays the enforcing check because the mirror
compiles out under `IR_RELEASE`. A guard that only re-checks what the type
already enforces (a count that only one bounded mutator writes) can fire on a
corrupt file but never on a live component, so it needs no mirror.
`SaveSerialize<C_Cycle>` and `SaveSerialize<C_TrianglesOnlySet>` are the
reference shapes.

## Why "cannot round-trip honestly" means opt out

A serializer that substitutes a default on load is a silent behavior change
wearing a round-trip's clothes: the save reports success, the load reports
success, and the entity behaves differently. Callables with no authored
identity to recover (`C_LambdaModifiers`, `C_LerpEntity`) and
`sol::protected_function` refs bound to one `lua_State`
(`C_EntityEventHandlers`) opt out with a comment saying why.

The design alternative is to **store the authored key, not the resolved
callable**: a component built from an enum keeps the enum and resolves it per
tick (`C_GotoEasing3D` / `C_RotationTarget` hold `IREasingFunctions` and look
up `kEasingFunctions`). Such a component stays trivially copyable and opts in
through the raw-image arm with no serializer at all.

## Why an explicit serializer on a trivially-copyable type is banned

A missing serializer header is loud today only because every explicit
`SaveSerialize<C>` specialization is on a non-trivially-copyable component:
the primary template is declared but never defined, so a TU that misses the
header sees an incomplete type, `SaveSerializable<C>` is false, and the
registry's `static_assert` fires.

A trivially-copyable component has the constrained partial specialization to
fall back on. A TU that misses its explicit-specialization header binds
**silently** to the raw-image arm: different bytes under the same save-name
and version, no diagnostic, and formally an ODR violation between the two
TUs. The rule is therefore structural, not stylistic. A hand-written layout
(skip a derived field, narrow an enum, drop padding) is reached by making the
component non-trivially-copyable, or by routing the exception through the
inventory.

## Why the load preserves the live render context

Every GPU handle (textures, SSBOs, framebuffers, pool residency) is
process-local, so those components opt out and come back holding no valid
GPU state. Reconstructing the render context on load would duplicate the
canvas bundle's construction path and race the render tick; instead the
`resetGameplay()` that precedes `loadWorld` keeps the `C_Persistent` canvas /
framebuffer / camera bundle and `C_VoxelPool` alive, and the loader never
touches them. The derived textures (lighting, AO, sun-shadow, fog) re-derive
from the pool each render tick for free.

The one gameplay-owned GPU state that must be rebuilt is `C_VoxelSetNew`'s
pool span. Its serializer persists only the authored
`{size, boundsMin, per-voxel records}` and reconstructs the set in staged
mode (`pendingVoxels_` populated, no pool span), which is what makes it safe
inside the loader's mutation-free validate pass. The authored records are not
always the live span: a GRID-mode set saved mid-rotation carries a
dest-lattice resample, so the serializer reads
`C_VoxelSetNew::rotationSourceVoxels_` when it is present — a save that lands
mid-spin round-trips the source arrangement, not the frame's resample.

`engine/world` must not depend on the voxel or render prefabs, so the seed
pass that moves staged sets into live pool spans (`SEED_STAGED_VOXELS`, or a
direct `attachToCanvas`) is registered by the **caller**, not baked into
`loadWorld`; `creations/demos/persist_roundtrip` is the reference.

## Why a serializer unit test is not enough

A standalone `SaveSerialize<C>` test proves the bytes round-trip; it does
not prove the registry entry, the Lua binding, and the reload are wired. A
component can pass its serializer test and still be absent from every
Lua-driven save. `test/script/lua_world_snapshot_test.cpp` drives the actual
`IRPersist` surface, so each new opted-in component adds a case there.

## The debug dump is a side-output

The `IR_PERSIST_DUMP` `.json.txt` writer is a second, richer walk
(archetype members plus `CHILD_OF` edges) emitted after the binary. It shares
the edge-collection helper with the binary `RELN` writer so both describe the
identical edge set, and it never feeds back into the binary, so the snapshot
is byte-identical with the flag on or off. It is distinct from the always-on
`.json` sidecar (a magic/version/count summary).
