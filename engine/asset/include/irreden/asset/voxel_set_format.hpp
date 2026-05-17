#ifndef IR_ASSET_VOXEL_SET_FORMAT_H
#define IR_ASSET_VOXEL_SET_FORMAT_H

/// `.vxs` v1 voxel-set asset format. Three coexisting persistence modes
/// share one container:
///
/// - **SHAPES** (T-168 / #665) — composition of SDF primitive instances;
///   rendered directly by `SHAPES_TO_TRIXEL` with no voxel pool allocation.
/// - **DENSE** (T-167 / #664) — bounded 3D voxel grid with per-voxel
///   records, named layer membership bitmasks, per-frame position-offset
///   poses, and free-form metadata. Round-trips `C_Voxel`-shaped data.
/// - **HYBRID** (T-668) — DENSE base plus SHAPES overrides.
///
/// On-disk container layout (built on `chunk_header.hpp`):
///
///     AssetHeader            { 'V', 'X', 'S', '1' | version | chunkCount }
///     ChunkTableEntry[]      one entry per chunk below
///     MODE chunk             4-byte mode tag (DENS | SHPS | HYBR)
///     SREF chunk             name-table for `IRMath::SDF::ShapeType`
///     SHPG chunk             shape-group primitive records (SHAPES mode)
///     BNDS chunk             dense bounds (ivec3 min, ivec3 max) (DENSE)
///     VOXR chunk             dense per-voxel records, 12 B/record (DENSE)
///     LAYR chunk             dense layer membership bitmasks (DENSE)
///     FRAM chunk             dense per-frame position offsets (DENSE)
///     META chunk             free-form key/value strings (DENSE)
///
/// Order in the chunk table is not load-bearing (Save Format Extensibility
/// Rule #1). A reader skips chunks it doesn't know without erroring; the
/// engine version drops to "older build receiving a future save" handling
/// when an unknown chunk tag, mode tag, or `ShapeType` id appears.
///
/// SHPG chunk body (one record per primitive instance):
///
///     varuint  shapeCount
///     repeat shapeCount times:
///       uint32  shapeTypeId        // numeric id from disk; cross-reference
///                                  // SREF to map to the current build's enum
///       uint16  recordVersion      // per-record additive-only versioning
///                                  // (Rule #3); v1 fields fixed below
///       float32 params[4]          // SDF parameter vector
///       uint32  packedRGBA         // `Color::toPackedRGBA()` packing
///       uint32  flags              // `IRMath::SDF::ShapeFlags` bit field
///       uint8   boneId             // joint binding (T-146/T-169); 0 = none
///       float32 offset[3]          // local translation
///       float32 rotation[4]        // quaternion (x, y, z, w)
///       uint8   csgOp              // `CsgOp` (see below)
///
/// `IRMath::SDF::ShapeType` values that resolve via name on load become
/// the current-build numeric id; unknown names/ids surface as
/// `unknownShapesSkipped_` and the rest of the asset loads cleanly.
///
/// BNDS chunk body — DENSE bounds:
///
///     int32 min.x, min.y, min.z, max.x, max.y, max.z   (6 × i32)
///
/// `min` is inclusive, `max` is exclusive — the voxel volume contains
/// `(max.x - min.x) × (max.y - min.y) × (max.z - min.z)` slots indexed
/// row-major in (x, y, z) order.
///
/// VOXR chunk body — DENSE per-voxel records:
///
///     uint16  recordVersion        // chunk-level version (Rule #3); v1
///                                  // fields fixed below
///     varuint count                // must equal volume from BNDS; loader
///                                  // validates and surfaces mismatches
///     repeat count times (12 B per record, matches `C_Voxel` layout):
///       uint32  packedRGBA         // `Color::toPackedRGBA()`
///       uint8   material_id
///       uint8   flags
///       uint8   bone_id
///       uint8   pad0               // reserved (zero on write)
///       uint32  reserved           // reserved for future per-voxel fields
///
/// LAYR chunk body — DENSE layer membership bitmasks (Phase 1):
///
///     varuint layerCount
///     repeat layerCount times:
///       string  name               // UTF-8 layer name
///       varuint bitmaskWordCount   // u64 words; expected
///                                  // ceil(voxelCount / 64)
///       uint64[bitmaskWordCount]   // packed bits, LE; 1 = member
///
/// FRAM chunk body — DENSE per-frame position offsets (Phase 1.4):
///
///     varuint frameCount
///     repeat frameCount times:
///       uint32  frameIndex
///       varuint offsetCount        // expected to equal voxelCount; a
///                                  // mismatch logs + drops the frame
///       float32[offsetCount][3]    // per-voxel position offset (x, y, z)
///
/// META chunk body — DENSE free-form metadata:
///
///     varuint entryCount
///     repeat entryCount times:
///       string  key
///       string  value
///
/// All strings are UTF-8 with a varuint byte-length prefix (see
/// `BinaryReader::readString`).

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/chunk_header.hpp>
#include <irreden/asset/name_table.hpp>
#include <irreden/ir_math.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace IRAsset {

using IRMath::Color;
using IRMath::ivec3;
using IRMath::vec3;
using IRMath::vec4;

constexpr std::array<char, 4> kVoxelSetMagic = {'V', 'X', 'S', '1'};
constexpr std::uint32_t kVoxelSetVersion = 1;

constexpr std::array<char, 4> kChunkTagMode = {'M', 'O', 'D', 'E'};
constexpr std::array<char, 4> kChunkTagShapeRefs = {'S', 'R', 'E', 'F'};
constexpr std::array<char, 4> kChunkTagShapeGroup = {'S', 'H', 'P', 'G'};
constexpr std::array<char, 4> kChunkTagBounds = {'B', 'N', 'D', 'S'};
constexpr std::array<char, 4> kChunkTagVoxelRecords = {'V', 'O', 'X', 'R'};
constexpr std::array<char, 4> kChunkTagLayers = {'L', 'A', 'Y', 'R'};
constexpr std::array<char, 4> kChunkTagFrames = {'F', 'R', 'A', 'M'};
constexpr std::array<char, 4> kChunkTagMeta = {'M', 'E', 'T', 'A'};

/// 4-byte ASCII tag stored as the MODE chunk body. Adding a future
/// mode is a new constant + a new branch in `readModeChunk`; older
/// builds receiving the new tag return `Unknown` and the caller falls
/// back to "skip body chunks for modes I don't recognize."
constexpr std::array<char, 4> kModeTagDense = {'D', 'E', 'N', 'S'};
constexpr std::array<char, 4> kModeTagShapes = {'S', 'H', 'P', 'S'};
constexpr std::array<char, 4> kModeTagHybrid = {'H', 'Y', 'B', 'R'};

enum class VoxelSetMode : std::uint8_t {
    DENSE = 0,
    SHAPES = 1,
    HYBRID = 2,
    UNKNOWN = 255, ///< Set when the on-disk mode tag isn't in the table.
};

/// Canonical textual form of a `VoxelSetMode` value for sidecar JSON and
/// log messages. Returns "UNKNOWN" for `VoxelSetMode::UNKNOWN` and for any
/// value not in the table.
constexpr std::string_view voxelSetModeToString(VoxelSetMode mode) noexcept {
    switch (mode) {
    case VoxelSetMode::DENSE:
        return "DENSE";
    case VoxelSetMode::SHAPES:
        return "SHAPES";
    case VoxelSetMode::HYBRID:
        return "HYBRID";
    case VoxelSetMode::UNKNOWN:
        return "UNKNOWN";
    }
    return "UNKNOWN";
}

/// Per-instance composition operator inside a shape group. NONE means
/// "render this primitive standalone"; the others form a flat
/// composition tree against the running accumulator in evaluation order.
/// Tree-shaped CSG would land in a separate `SHPT` chunk later without
/// bumping the file version.
enum class CsgOp : std::uint8_t {
    NONE = 0,
    UNION = 1,
    SMOOTH_UNION = 2,
    SUBTRACT = 3,
    INTERSECT = 4,
};

/// v1 SHPG record layout version. Bump per Rule #3 when fields are
/// added; older loaders default the appended fields. Removed/renamed
/// fields need an explicit migration keyed by `(recordType, oldVersion)`.
constexpr std::uint16_t kShapeRecordVersion = 1;

/// One persisted SDF primitive instance. Mirrors `C_ShapeDescriptor`'s
/// on-screen fields plus the composition metadata (`offset_`,
/// `rotation_`, `boneId_`, `csgOp_`) the runtime caller decides whether
/// to map onto separate components or fold into the descriptor itself.
// IRAsset: serialized
struct ShapeRecord {
    static constexpr std::uint16_t kSaveVersion = kShapeRecordVersion;
    /// Numeric id read from the SHPG chunk. After `readShapeGroupChunk`
    /// resolves the SREF name table, this holds the **current build's**
    /// `IRMath::SDF::ShapeType` value; the on-disk id is dropped once
    /// the mapping succeeds.
    std::uint32_t shapeTypeId_ = 0;
    std::uint16_t recordVersion_ = kShapeRecordVersion;
    vec4 params_ = vec4(1.0f);
    Color color_ = Color{255, 255, 255, 255};
    std::uint32_t flags_ = 0;
    std::uint8_t boneId_ = 0;
    vec3 offset_ = vec3(0.0f);
    /// Quaternion (x, y, z, w). Default is identity.
    vec4 rotation_ = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CsgOp csgOp_ = CsgOp::NONE;
};

// ---- DENSE-mode types --------------------------------------------------

/// VOXR chunk-level version (Rule #3). Bump when the on-disk record
/// layout changes; older loaders default the appended fields.
/// v1: byte 7 was pad0_ (always zero). v2: byte 7 is layer_id_
/// (per-voxel layer membership; 0 = default layer). Wire bytes are
/// identical for pre-layer files; the version gate in
/// readVoxelRecordsChunk makes the migration explicit (Rule #3).
constexpr std::uint16_t kVoxelRecordVersion = 2;

/// 12 B per-voxel record. Layout MUST match `C_Voxel` (in
/// `engine/prefabs/irreden/voxel/components/component_voxel.hpp`) so
/// the runtime can copy disk → memory without per-field translation.
/// `engine/asset/` lives below the prefab layer, so the format owns
/// its own struct rather than including the component header.
// IRAsset: serialized
struct VoxelRecord {
    static constexpr std::uint16_t kSaveVersion = kVoxelRecordVersion;

    Color color_ = Color{0, 0, 0, 0};
    std::uint8_t material_id_ = 0;
    std::uint8_t flags_ = 0;
    std::uint8_t bone_id_ = 0;
    std::uint8_t layer_id_ = 0;
    std::uint32_t reserved_ = 0;
};

static_assert(sizeof(VoxelRecord) == 12, "VoxelRecord must be 12 B (matches C_Voxel)");

/// Named voxel-slot membership bitmask. `bitmask_` is 1 bit per slot,
/// packed LE into u64 words; word count is `ceil(voxelCount / 64)`.
struct LayerInfo {
    std::string name_;
    std::vector<std::uint64_t> bitmask_;
};

/// Per-frame position offset for every voxel slot. `offsets_.size()`
/// must equal the dense voxel volume; a mismatch logs and drops the
/// frame (Rule #5: unknown is recoverable).
struct FramePose {
    std::uint32_t frameIndex_ = 0;
    std::vector<vec3> offsets_;
};

/// Free-form `(key, value)` metadata pair. Both UTF-8 strings.
struct MetaEntry {
    std::string key_;
    std::string value_;
};

/// In-memory representation of a DENSE-mode `.vxs` file. Bounds are
/// inclusive-min / exclusive-max; the voxel volume contains
/// `voxelCount()` slots indexed row-major in (x, y, z) order.
struct DenseVoxelSet {
    ivec3 boundsMin_ = ivec3(0);
    ivec3 boundsMax_ = ivec3(0);
    std::uint16_t recordVersion_ = kVoxelRecordVersion;
    std::vector<VoxelRecord> voxels_;
    std::vector<LayerInfo> layers_;
    std::vector<FramePose> frames_;
    std::vector<MetaEntry> meta_;

    /// Bounds-derived voxel slot count. Adversarially large positive
    /// dimensions can wrap `std::size_t` (three values near INT32_MAX
    /// product to ~9.9e27, which exceeds u64). The wrap is benign in
    /// practice because the downstream `readVoxelRecordsChunk` reserve
    /// cap (`remaining_bytes / 12`) bounds any actual allocation, so
    /// the only observable effect is a spurious VOXR count-mismatch
    /// warning. Callers indexing `DenseVoxelSet::voxels_` must still
    /// validate `voxels_.size() == voxelCount()` before random access.
    std::size_t voxelCount() const {
        const ivec3 d = boundsMax_ - boundsMin_;
        if (d.x <= 0 || d.y <= 0 || d.z <= 0) {
            return 0;
        }
        return static_cast<std::size_t>(d.x) * static_cast<std::size_t>(d.y) *
               static_cast<std::size_t>(d.z);
    }
};

// ---- MODE chunk --------------------------------------------------------

/// Build a MODE chunk payload from a known mode. `VoxelSetMode::UNKNOWN`
/// is rejected (returns an empty payload with a log) — callers should
/// never persist an unknown mode.
ChunkPayload makeModeChunk(VoxelSetMode mode);

/// Inspect the chunk list for a MODE chunk. Missing chunk → returns
/// `SHAPES` (legacy default — the only writer in v1.0 emits shape
/// groups). Unknown tag → returns `UNKNOWN` with a one-line log.
VoxelSetMode readModeChunk(std::span<const LoadedChunk> chunks);

// ---- SREF chunk --------------------------------------------------------

/// Build a SREF chunk payload from `(id, name)` pairs covering every
/// `IRMath::SDF::ShapeType` value the writer's build knows about.
ChunkPayload makeShapeRefsChunk(std::span<const NameTableEntry> entries);

/// Convenience: emit the SREF entries for the **current build's**
/// `IRMath::SDF::ShapeType` enum. Add to this list whenever the enum
/// gains a value.
std::vector<NameTableEntry> buildCurrentShapeTypeNameTable();

/// Parse a SREF chunk body.
Result<std::vector<NameTableEntry>> readShapeRefsChunk(std::span<const std::uint8_t> body);

// ---- SHPG chunk --------------------------------------------------------

/// Serialize @p records to a SHPG chunk body.
ChunkPayload makeShapeGroupChunk(std::span<const ShapeRecord> records);

/// One read result: the records the loader could resolve, plus the
/// count of records dropped because their disk-side `ShapeType` id /
/// name resolves to nothing in the current build. The skip count is
/// surfaced for diagnostics (Rule #5: unknown is recoverable).
struct ShapeGroupLoadResult {
    std::vector<ShapeRecord> records_;
    std::size_t unknownShapesSkipped_ = 0;
};

/// Parse a SHPG chunk body. @p diskShapeTypes maps disk-side ids to
/// names (built from the file's SREF chunk). A record whose disk id
/// resolves to a name absent from `buildCurrentShapeTypeNameTable()`
/// is dropped and counted in `unknownShapesSkipped_`. If
/// @p diskShapeTypes is empty, the disk-side numeric id is taken
/// verbatim (legacy save with no SREF chunk).
Result<ShapeGroupLoadResult>
readShapeGroupChunk(std::span<const std::uint8_t> body, const NameTable &diskShapeTypes);

// ---- BNDS chunk --------------------------------------------------------

/// Build a BNDS chunk payload (6 × i32: min.xyz + max.xyz).
ChunkPayload makeBoundsChunk(const ivec3 &boundsMin, const ivec3 &boundsMax);

struct BoundsPair {
    ivec3 boundsMin_ = ivec3(0);
    ivec3 boundsMax_ = ivec3(0);
};

/// Parse a BNDS chunk body. Returns `Truncated` if the body is < 24 B.
Result<BoundsPair> readBoundsChunk(std::span<const std::uint8_t> body);

// ---- VOXR chunk --------------------------------------------------------

/// Serialize @p voxels into a VOXR chunk body. Writes `kVoxelRecordVersion`
/// + varuint count + tightly-packed 12 B records.
ChunkPayload makeVoxelRecordsChunk(std::span<const VoxelRecord> voxels);

struct VoxelRecordsLoadResult {
    std::vector<VoxelRecord> voxels_;
    std::uint16_t recordVersion_ = kVoxelRecordVersion;
};

/// Parse a VOXR chunk body. @p expectedCount comes from the BNDS
/// volume; a mismatch is logged and the loader trusts the chunk's own
/// varuint count (Rule #5 — recoverable, never fatal).
Result<VoxelRecordsLoadResult>
readVoxelRecordsChunk(std::span<const std::uint8_t> body, std::size_t expectedCount);

// ---- LAYR chunk --------------------------------------------------------

/// Serialize @p layers into a LAYR chunk body.
ChunkPayload makeLayersChunk(std::span<const LayerInfo> layers);

/// Parse a LAYR chunk body.
Result<std::vector<LayerInfo>> readLayersChunk(std::span<const std::uint8_t> body);

// ---- FRAM chunk --------------------------------------------------------

/// Serialize @p frames into a FRAM chunk body. Each frame writes
/// `frameIndex + varuint offsetCount + offsetCount × vec3` — the count
/// is taken from `frames[i].offsets_.size()` so a hand-built frame with
/// a mismatched offset count round-trips faithfully (the loader logs
/// the mismatch).
ChunkPayload makeFramesChunk(std::span<const FramePose> frames);

struct FramesLoadResult {
    std::vector<FramePose> frames_;
    /// Frames dropped because their on-disk per-voxel offset count did
    /// not match the dense voxel volume. Surfaced for diagnostics.
    std::size_t skippedFrames_ = 0;
};

/// Parse a FRAM chunk body. A frame whose `offsetCount != voxelCount`
/// is read in full (so the next frame's offset is correct) but dropped
/// from the result; the skip count surfaces in `skippedFrames_`.
Result<FramesLoadResult>
readFramesChunk(std::span<const std::uint8_t> body, std::size_t voxelCount);

// ---- META chunk --------------------------------------------------------

/// Serialize @p entries into a META chunk body.
ChunkPayload makeMetaChunk(std::span<const MetaEntry> entries);

/// Parse a META chunk body.
Result<std::vector<MetaEntry>> readMetaChunk(std::span<const std::uint8_t> body);

// ---- High-level save/load ----------------------------------------------

/// Save a shape-group `.vxs` asset to @p path. Writes MODE=SHAPES,
/// SREF, and SHPG chunks. Also emits a `.vxs.json` sidecar at
/// `path + ".json"` (Rule #6) when the binary write succeeds; a failed
/// binary write skips the sidecar so a stale sidecar never outlives a
/// missing binary.
BinaryStatus saveShapeGroup(const std::string &path, std::span<const ShapeRecord> records);

/// Loader-side view of a shape-group `.vxs` file.
struct VoxelSetFile {
    VoxelSetMode mode_ = VoxelSetMode::UNKNOWN;
    std::vector<ShapeRecord> shapeRecords_;
    std::size_t unknownShapesSkipped_ = 0;
};

/// Load a shape-group `.vxs` asset from @p path. Returns:
/// - `BadMagic` if magic isn't `VXS1`
/// - `VersionTooNew` if file version > `kVoxelSetVersion`
/// - underlying primitive error for truncated / out-of-bounds chunks
/// - `ShapeGroupLoadResult` populated with resolved records and the
///   count of dropped unknowns otherwise.
///
/// Mode-aware: DENSE/HYBRID chunks present in a future save are
/// surfaced through `mode_` but their record arrays are empty for v1
/// (loaders for those modes land in T-167/T-668).
Result<VoxelSetFile> loadShapeGroup(const std::string &path);

/// Save a DENSE-mode `.vxs` asset to @p path. Writes MODE=DENSE plus
/// BNDS / VOXR / LAYR / FRAM / META chunks. Empty optional sections
/// (no layers / no frames / no meta) omit the corresponding chunk so
/// older loaders need no special-casing. Also emits a `.vxs.json`
/// sidecar at `path + ".json"` (Rule #6) when the binary write
/// succeeds; the sidecar's `shape_primitives_summary` is empty for
/// DENSE saves.
BinaryStatus saveDenseVoxelSet(const std::string &path, const DenseVoxelSet &dense);

/// Loader-side view of a DENSE-mode `.vxs` file. `mode_` is read from
/// the MODE chunk; `dense_` is populated from BNDS + VOXR + LAYR +
/// FRAM + META. `skippedFrames_` carries the FRAM-chunk diagnostic.
///
/// Per Extensibility Rule #5 (unknown is recoverable, never fatal),
/// partial loads can leave `dense_.voxels_.size() != dense_.voxelCount()`
/// — e.g. a malformed file with BNDS present but VOXR absent loads as
/// `dense_.voxels_.empty()` with `dense_.voxelCount() > 0`, plus a
/// warning log. **Callers that index `dense_.voxels_` must validate
/// `voxels_.size() == dense_.voxelCount()` before random access.**
struct DenseVoxelSetFile {
    VoxelSetMode mode_ = VoxelSetMode::UNKNOWN;
    DenseVoxelSet dense_;
    std::size_t skippedFrames_ = 0;
};

/// Load a DENSE-mode `.vxs` asset from @p path. Returns:
/// - `BadMagic` if magic isn't `VXS1`
/// - `VersionTooNew` if file version > `kVoxelSetVersion`
/// - `Truncated` / `ChunkOutOfBounds` for malformed chunk tables
/// - `DenseVoxelSetFile` with the resolved dense data otherwise.
///
/// SHAPES-only files (no BNDS / VOXR chunks present) load as `mode_ =
/// VoxelSetMode::SHAPES` with an empty `dense_`. Callers that need both
/// halves should use `loadVoxelSet` instead.
Result<DenseVoxelSetFile> loadDenseVoxelSet(const std::string &path);

// ---- Unified HYBRID save/load -------------------------------------------

/// Combined loader result covering all three `.vxs` modes (SHAPES, DENSE,
/// HYBRID). For SHAPES-only saves `dense_` is default-constructed (empty
/// bounds and voxels). For DENSE-only saves `shapeRecords_` is empty. For
/// HYBRID both halves are populated. Callers inspect `mode_` to decide
/// which entities to spawn.
struct VoxelSetAllFile {
    VoxelSetMode mode_ = VoxelSetMode::UNKNOWN;
    std::vector<ShapeRecord> shapeRecords_;
    std::size_t unknownShapesSkipped_ = 0;
    DenseVoxelSet dense_;
    std::size_t skippedFrames_ = 0;
};

/// Save a HYBRID-mode `.vxs` asset to @p path. Writes MODE=HYBRID, SREF,
/// SHPG, BNDS, VOXR, and (if non-empty) LAYR/FRAM/META chunks. Also emits
/// a `.vxs.json` sidecar at `path + ".json"` (Save Format Extensibility
/// Rule #6 — sidecar regenerated from the binary on every save, never the
/// source of truth). The sidecar is not emitted when the binary write fails.
BinaryStatus saveVoxelSet(
    const std::string &path, std::span<const ShapeRecord> shapes, const DenseVoxelSet &dense
);

/// Load any `.vxs` asset and populate both the shape-group and dense
/// halves. Returns:
/// - `BadMagic` if magic isn't `VXS1`
/// - `VersionTooNew` if file version > `kVoxelSetVersion`
/// - `Truncated` / `ChunkOutOfBounds` for malformed chunk tables
/// - `VoxelSetAllFile` with all available halves populated on success.
///
/// SHAPES-only files leave `dense_` empty; DENSE-only files leave
/// `shapeRecords_` empty; HYBRID files populate both.
Result<VoxelSetAllFile> loadVoxelSet(const std::string &path);

} // namespace IRAsset

#endif /* IR_ASSET_VOXEL_SET_FORMAT_H */
