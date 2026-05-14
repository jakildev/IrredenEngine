#ifndef IR_ASSET_RIG_FORMAT_H
#define IR_ASSET_RIG_FORMAT_H

/// `.rig` v1 — joint hierarchy + bind points for skeletal voxel rigs.
/// Chunks are added without bumping the file version; older readers
/// silently skip unknown tags (Save Format Extensibility Rule #1).
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
///     BIND chunk body (optional — omitted when no bind points):
///         varuint  bindPointCount
///         repeat bindPointCount times:
///             uint16  recordVersion    (Rule #3, per-record additive-only)
///             uint32  boneId
///             float32 offset { x, y, z }
///             float32 rotation { x, y, z, w }
///             string  name             // varuint-prefixed UTF-8, may be empty
///
/// Sidecar (regenerated from binary on every save, ignored on load —
/// Rule #6):
///
///     <path>/<name>.rig.json    per-joint { index, name, parentIndex }
///                               per-bind-point { index, name, boneId }
///
/// Version history
/// ---------------
/// v1 (initial) — joints-only via JNTS chunk.
/// v1 + BIND chunk (#669) — bind points added as a second chunk;
///   no version bump (Rule #1 — older readers skip BIND silently).
/// Reserved chunk tags for forward compatibility (not yet written):
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

/// A named attachment point on a rig. Records the bone the point is
/// parented to, a local-space offset (vec3) and orientation (quaternion)
/// relative to that bone, and an optional designer-facing name. Used by
/// the editor (#669) to define where props, weapons, or VFX attach.
// IRAsset: serialized
struct RigBindPoint {
    static constexpr std::uint16_t kSaveVersion = 1;

    std::uint32_t boneId_ = 0;
    IRMath::vec3 offset_{0.0f, 0.0f, 0.0f};
    IRMath::vec4 rotation_{0.0f, 0.0f, 0.0f, 1.0f};
    std::string name_;
};

/// Asset-side rig — a flat list of joints and optional bind points.
/// Conversion to/from `IRComponents::C_JointHierarchy` lives in the
/// prefab bridge at `engine/prefabs/irreden/voxel/rig_bridge.hpp` so
/// `engine/asset/` has no dependency on the voxel component.
struct Rig {
    std::vector<RigJoint> joints_;
    std::vector<RigBindPoint> bindPoints_;
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
