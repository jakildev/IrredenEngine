#ifndef IR_ASSET_VOXEL_SET_FORMAT_H
#define IR_ASSET_VOXEL_SET_FORMAT_H

/// `.vxs` v1 voxel-set asset format. Two coexisting persistence modes
/// share one container:
///
/// - **SHAPES** (this header, T-168 / #665) — composition of SDF
///   primitive instances; rendered directly by `SHAPES_TO_TRIXEL` with
///   no voxel pool allocation.
/// - **DENSE** (T-167 / #664) — bounded 3D voxel grid with per-voxel
///   records.
/// - **HYBRID** (T-668) — DENSE base plus SHAPES overrides.
///
/// On-disk container layout (built on `chunk_header.hpp`):
///
///     AssetHeader            { 'V', 'X', 'S', '1' | version | chunkCount }
///     ChunkTableEntry[]      one entry per chunk below
///     MODE chunk             4-byte mode tag (DENS | SHPS | HYBR)
///     SREF chunk             name-table for `IRMath::SDF::ShapeType`
///     SHPG chunk             shape-group primitive records (SHAPES mode)
///     [BNDS | VOXR | LAYR | FRAM | META chunks added by T-167 for DENSE]
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
///       uint32  flags              // `IRRender::ShapeFlags` bit field
///       uint8   boneId             // joint binding (T-146/T-169); 0 = none
///       float32 offset[3]          // local translation
///       float32 rotation[4]        // quaternion (x, y, z, w)
///       uint8   csgOp              // `CsgOp` (see below)
///
/// `IRMath::SDF::ShapeType` values that resolve via name on load become
/// the current-build numeric id; unknown names/ids surface as
/// `unknownShapesSkipped_` and the rest of the asset loads cleanly.

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/chunk_header.hpp>
#include <irreden/asset/name_table.hpp>
#include <irreden/ir_math.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace IRAsset {

using IRMath::Color;
using IRMath::vec3;
using IRMath::vec4;

constexpr std::array<char, 4> kVoxelSetMagic = {'V', 'X', 'S', '1'};
constexpr std::uint32_t kVoxelSetVersion = 1;

constexpr std::array<char, 4> kChunkTagMode = {'M', 'O', 'D', 'E'};
constexpr std::array<char, 4> kChunkTagShapeRefs = {'S', 'R', 'E', 'F'};
constexpr std::array<char, 4> kChunkTagShapeGroup = {'S', 'H', 'P', 'G'};

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
struct ShapeRecord {
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
Result<ShapeGroupLoadResult> readShapeGroupChunk(
    std::span<const std::uint8_t> body,
    const NameTable &diskShapeTypes
);

// ---- High-level save/load ----------------------------------------------

/// Save a shape-group `.vxs` asset to @p path. Writes MODE=SHAPES,
/// SREF, and SHPG chunks. Returns the writer's failure state.
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

} // namespace IRAsset

#endif /* IR_ASSET_VOXEL_SET_FORMAT_H */
