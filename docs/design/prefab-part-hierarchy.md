# Prefab part hierarchy

Schema v3 adds an optional `parent` part id. Omission attaches the part to
the prefab root. The transform is local to that parent; the voxel set keeps
its own bounds, anchor, and rendering mode. Parenting never merges voxel
grids or rewrites geometry.

```lua
return {
    prefab_version = 3,
    parts = {
        { id = "support", shape = { type = IRShape.BOX } },
        {
            id = "ornament",
            parent = "support",
            shape = { type = IRShape.SPHERE },
            transform = { translation = { 0, 0, 4 } },
        },
    },
}
```

Part order is not hierarchy order: a child may precede its parent in the
manifest. The parser resolves stable ids, rejects missing parents and
cycles before creating entities, and retains a parent-first traversal for
runtime construction. v1 runtime prefabs and flat v2 manifests still load;
a `parent` field in v2 is rejected instead of being silently ignored.

## LOD lifetime and visibility

Each part keeps its own inclusive LOD band. A visible descendant needs all
of its ancestors for transform composition, even when an ancestor's own
content is out of band. The runtime retains those ancestor entities and
pins only their own content to the root's settled tier. It does not apply
subtree visibility suppression, so an out-of-band parent does not hide an
in-band child. Components on a retained ancestor remain alive too.

The existing resident policy still applies: a resident part remains alive
after its first spawn. Its ancestors are retained with it. Nonresident
branches with no needed descendants are removed child-first. Spawn budgets
count ancestors as well as visible descendants; parents are staged before
children, and a child waits if its parent cannot be staged within the
current budget.

The executor is `PrefabParts.NestedLodRetainsHiddenAncestorsAndRebuildsParentFirst`
in `test/script/prefab_parts_test.cpp`. This proves entity lifetime and
content LOD policy, not visual correctness of every detached-rendering mode.

## Editor behavior

`EntityScene::reparentPart` preserves the authored world pose by default,
using the engine's SQT composition convention. It refuses cycles, absent
parents, and preservation under a zero-scale parent. Passing
`preserveWorld = false` keeps the local pose instead. Simulation modifiers
are not baked into authored geometry or transforms.

Deleting a part detaches its direct children to the scene root while
preserving their authored world poses. The deletion snapshot records their
former local poses and associations, so restoring the deleted part restores
those associations too. Cloning/arraying a part keeps the source parent;
array offsets are therefore in the common parent's local coordinates.

Nested scenes save as v3. Flat scenes continue to save as v2. Recipe
bindings and component records remain attached to stable part ids.

The editor API and round-trip contract are covered by the
`PrefabWriter.EntitySceneReparentPreservesWorldAndRejectsCycles` and
`PrefabWriter.EntitySceneHierarchyRoundTripCloneAndRemovalUndo` tests.
Viewport editing of a rotated part is a separate tool concern: a hierarchy
does not by itself make an axis-aligned voxel picker handle local canvases.
