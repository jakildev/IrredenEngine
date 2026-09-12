# engine/asset/ — persistent asset formats

This module owns reusable binary/text asset formats and the binary-I/O
primitives shared by them. ECS world snapshots stay in `engine/world/` because
they traverse archetypes; `engine/asset/` must remain below `world/`, `entity/`,
and `prefabs/` in the dependency graph.

## Commands

```bash
fleet-build --target IrredenEngineTest
fleet-run IrredenEngineTest --gtest_brief=1
python3 scripts/lint_comment_refs.py
python3 scripts/lint_instruction_size.py
```

See [`docs/agents/VALIDATION.md`](../../docs/agents/VALIDATION.md) for what each
validator proves.

## Format ownership

Wire layouts, chunk catalogs, version histories, and public entry points belong
in the format headers, not here:

| Format | Canonical contract |
|---|---|
| `.irsprite` | [`ir_asset.hpp`](include/irreden/ir_asset.hpp) |
| `.vxs` | [`voxel_set_format.hpp`](include/irreden/asset/voxel_set_format.hpp) |
| `.rig` | [`rig_format.hpp`](include/irreden/asset/rig_format.hpp) |
| `.irkv` | [`key_value_store.hpp`](include/irreden/asset/key_value_store.hpp) |

Update the owning header block whenever its format changes. The long-form
rationale for forward-compatible formats lives in
[`docs/design/entity-editor-epic.md`](../../docs/design/entity-editor-epic.md#save-format-extensibility-rules).

## Binary-format contracts

- New binary formats use `FileBinaryWriter` / `FileBinaryReader`, or their
  memory-backed equivalents. Do not add raw `fopen` + `fwrite` / `fread`
  format paths.
- Reads return `Result<T>` with a recoverable diagnostic carrying source and
  byte offset. Unknown chunks are skipped; unknown record contents that cannot
  be length-skipped fail the containing load.
- A loader that writes live engine state fully decodes and validates the
  payload before its first mutation, or stages the result and splices it only
  after complete success.
- JSON sidecars are generated from the binary on save and ignored on load.
  They are for inspection, never a second source of truth.
- Registered enums persist as numeric id plus a name table. Readers prefer
  name-to-current-enum lookup and use the id only when the table is absent.

## Serialized-record evolution

A struct whose fields are serialized individually into an asset record has
this exact pair:

```cpp
// IRAsset: serialized
struct Record {
    static constexpr std::uint16_t kSaveVersion = 1;
};
```

The annotation is immediately above the declaration. Field additions append
and bump `kSaveVersion`; field removals or renames also add an explicit reader
migration. Update the format header's version history in the same change.
Framework transport types such as `AssetHeader`, `ChunkTableEntry`,
`LoadedChunk`, and `Result<T>` are governed by the file format rather than this
record annotation.

The `simplify` check reports changed annotated layouts without a version bump
and newly serialized record types missing the annotation. Findings require a
human-chosen version and migration; do not auto-fix them.

## Format-specific contracts

- `.irsprite` is a line-oriented PNG-atlas sidecar. Atlas dimensions come from
  the PNG. Animation names contain no whitespace; the writer asserts this.
- Asset rigs remain independent of runtime prefab components. Runtime
  conversion belongs in prefab-side bridges, not this module.
- `.irkv` is a flat Lua-facing store of numbers, booleans, strings, and flat
  scalar lists. Callers pass a full path; persistent user saves belong under
  `IRUtility::userDataDir(...)`. Malformed or missing stores load as empty
  defaults. It has no sidecar.
- Adding an SDF shape updates the `ShapeType` enum, the asset-side shape name
  table, the CPU evaluator/bounds dispatch, and both shader backends. Adding a
  shape does not change the `.vxs` container version.

## Deprecated

The `.vxs` SHAPES/HYBRID writers `saveShapeGroup`, the shape-record overload of
`saveVoxelSet`, and the prefab `C_ShapeDescriptor` adapter are legacy write
surfaces. New assets use `saveDenseVoxelSet`; readers remain for existing
assets. See
[`docs/design/sdf-migration-plan.md`](../../docs/design/sdf-migration-plan.md).

## Pitfalls

- Compose paths with `IRUtility::joinPath`; platform separators differ.
- This is a format module, not a general asset pipeline. Shader hot reload,
  audio decoding, and model loading need their owning modules or a design pass.
- `FileTypes::kVoxelImage` has no load/save implementation. Do not treat the
  enum entry as a usable format.
