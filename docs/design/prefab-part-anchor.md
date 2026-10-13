# Prefab part pivot and anchor invariant

A voxel part rotates about its entity origin: the translation in
`C_LocalTransform::translation_`. The engine has no second part pivot. A part
that rotates is therefore `EntityAnchor::CENTER`, and every voxel part the
editor creates uses that anchor.

The anchor belongs to the prefab part manifest, beside `rotation_mode`. The
`.vxs` owns the voxel content, extent, and integer bounds; it does not own the
semantic attachment point. This split is part of the
[`parts` schema](lod-strategy.md) Phase 2 field table.

## Invariant

- GRID rotation transforms each authored local voxel about the entity origin
  and then adds `C_LocalTransform::translation_` in
  `IRPrefab::GridRotation::worldCellForGridVoxel`.
- DETACHED and DETACHED_REVOXELIZE rotate their private pool about the pool
  origin. `REBUILD_DETACHED_VOXELS` requires a centered body before creating
  its static bound, so an asymmetric CORNER or GROUND body is refused rather
  than allowed to orbit its anchor.
- A rotating prefab part is `EntityAnchor::CENTER`. Editor creation, cloning,
  restoration, preview, save, and reload preserve CENTER.
- A `voxel_ref` part records `anchor` as an
  `IRComponent.EntityAnchor.*` integer. An absent part anchor reads CORNER;
  this keeps legacy manifests and fixtures unchanged. Roots, shape parts, and
  bare parts do not accept the field.

`anchorLocalCenter()` is the body-center accessor. It does not define another
pivot; it derives a point within the local layout owned by the anchor.

## Why the manifest owns the anchor

The DENSE `.vxs` format stores integer bounds. That can represent the authored
CORNER origin directly, but it cannot represent the half-integer local origin
of an even-sized CENTER set. `C_VoxelSetNew::stagedOrigin` therefore preserves
the authored integer bound for CORNER and derives every non-CORNER origin with
`anchorOffset(anchor_, size_)`.

For example, a CENTER set with an even extent has local limits separated by
half-integer coordinates. Serializing a rounded origin would move it on reload.
Serializing the extent in `.vxs` and the CENTER anchor in the manifest instead
reconstructs the same half-integer origin exactly.

This ownership also keeps the boundary defaults distinct:

- an absent prefab-part `anchor` means CORNER for compatibility;
- `IRAsset.loadVoxelSet(path)` defaults its optional anchor argument to CENTER
  for direct Lua authoring.

## Conversion consumers

Every `IRPrefab::DenseVoxel::toComponent` bridge must choose and pass the
anchor owned by its calling surface. The current consumers are:

| Surface | Symbol | Anchor source |
|---|---|---|
| Prefab spawn | `attachVoxelContent` | `PrefabPartDescription::anchor_` through the part specification; the root passes CORNER explicitly. |
| Editor clone and arrays | `EntityScene::cloneSelected` | the source `C_VoxelSetNew::anchor_`. |
| Editor undo restore | `EntityScene::restorePart` | `RemovedEditorPart::voxelAnchor_`. |
| Editor manifest load | `EntityScene::load` | `PrefabPartDescription::anchor_`. |
| Editor mode-preview twin | `createModePreviewTwin` | the source `C_VoxelSetNew::anchor_`. |
| Lua asset loader | `IRAsset.loadVoxelSet` | the validated optional argument, defaulting to CENTER. |

Editor save is the inverse bridge: `EntityScene::save` writes the live
`C_VoxelSetNew::anchor_` into `PrefabPartDescription::anchor_`, and
`writeManifest` emits it beside `rotation_mode` for a `voxel_ref` part.

## Migration status

Editor-created rotating parts are CENTER and retain that anchor through clone,
undo restore, mode preview, save, and reload. Existing manifests without the
field remain CORNER, including the checked-in legacy fixtures. They are not
silently recentered. If such a part is not centered, editor mode changes and
manifest loading keep it out of DETACHED_REVOXELIZE rather than allowing it to
orbit the pool origin.

Components carry anchor data. Prefab and editor helpers choose and propagate
it; systems enforce the rotation-mode invariants. `C_VoxelSetNew` derives only
its own local layout and does not own cross-entity placement policy.

## What to verify

When adding or changing a prefab-part content bridge:

1. Confirm every `DenseVoxel::toComponent` call passes the surface's anchor
   explicitly and that the inverse save path records the live anchor.
2. Cover absent manifest `anchor` as CORNER and reject `anchor` where no
   `voxel_ref` content exists.
3. Exercise an even-sized CENTER part across save and reload; its derived
   half-integer origin and occupied world cells must be unchanged.
4. Exercise GRID, DETACHED, and DETACHED_REVOXELIZE around the part's
   translation. An uncentered set must be refused by the revoxelize path.
