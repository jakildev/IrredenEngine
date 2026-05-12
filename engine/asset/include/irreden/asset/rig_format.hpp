#ifndef IR_ASSET_RIG_FORMAT_H
#define IR_ASSET_RIG_FORMAT_H

/// `.rig` v1 — joint hierarchy for skeletal voxel rigs. First slice of
/// the rig asset format; subsequent issues add chunks for bind points
/// (#669) and animation keyframes (#606) without bumping `.rig` v1 —
/// older readers silently skip the new tags (Save Format Extensibility
/// Rule #1).
///
/// `.rig` is intentionally separate from `.vxs` so the same skeleton can
/// be shared across voxel variants (same rig, different paint job).
///
/// On-disk layout
/// --------------
///
///     AssetHeader  { magic = "IRRG", version = 1, chunkCount }
///     ChunkTableEntry[chunkCount]
///     JNTS chunk body:
///         varuint  jointCount
///         repeat jointCount times:
///             uint16  recordVersion    (Rule #3, per-record additive-only)
///             float32 rotation { x, y, z, w }   // quaternion as { qw, qx, qy, qz }
///             float32 translation { x, y, z, w }
///             uint32  parentIndex
///             string  name             // varuint-prefixed UTF-8, may be empty
///
/// Sidecar (regenerated from binary on every save, ignored on load —
/// Rule #6):
///
///     <path>/<name>.rig.json    per-joint { index, name, parentIndex }
///
/// Version history
/// ---------------
/// v1 (initial) — joints-only via JNTS chunk.
/// Reserved chunk tags for forward compatibility (not yet written):
///   - "BIND" — bind points (#669)
///   - "ANIM" — animation keyframes (#606)

#include <irreden/asset/binary_io.hpp>

#include <irreden/ir_math.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace IRAsset {

constexpr std::array<char, 4> kRigMagic{'I', 'R', 'R', 'G'};
constexpr std::uint32_t kRigFormatVersion = 1;

/// Per-joint record version. Field additions append + bump the version;
/// the load path defaults the appended fields. See Save Format
/// Extensibility Rule #3.
constexpr std::uint16_t kJointRecordVersion = 1;

/// Asset representation of a single joint. Mirrors `IRComponents::Joint`
/// (engine/prefabs/irreden/voxel/components/component_joint_hierarchy.hpp)
/// with the addition of an optional designer-facing name. The rotation
/// `vec4` carries a quaternion in `{ qw, qx, qy, qz }` order — same
/// convention as the runtime component and the GPU upload struct.
struct RigJoint {
    IRMath::vec4 rotation_{0.0f, 0.0f, 0.0f, 1.0f};
    IRMath::vec4 translation_{0.0f, 0.0f, 0.0f, 0.0f};
    std::uint32_t parentIndex_ = 0;
    std::string name_;
};

/// Asset-side rig — a flat list of joints. Conversion to/from the
/// runtime `IRComponents::C_JointHierarchy` lives in the prefab bridge
/// at `engine/prefabs/irreden/voxel/rig_bridge.hpp` so `engine/asset/`
/// has no dependency on the voxel component.
struct Rig {
    std::vector<RigJoint> joints_;
};

/// File-mode save. Writes `<path>/<name>.rig` plus a designer-readable
/// `<path>/<name>.rig.json` sidecar. Returns the writer's failure state.
BinaryStatus saveRig(const std::string &name, const std::string &path, const Rig &rig);

/// File-mode load. Returns an empty `Rig` with a recoverable error when
/// the file cannot be opened, has bad magic, or carries a version above
/// `kRigFormatVersion`. Unknown chunks are silently skipped (Rule #1).
Result<Rig> loadRig(const std::string &name, const std::string &path);

/// Buffer-mode write — exposed so tests (and any future in-memory
/// consumer such as network-streamed rigs) can exercise the format
/// without touching disk.
BinaryStatus writeRig(BinaryWriter &w, const Rig &rig);

/// Buffer-mode read counterpart.
Result<Rig> readRig(BinaryReader &r);

} // namespace IRAsset

#endif /* IR_ASSET_RIG_FORMAT_H */
