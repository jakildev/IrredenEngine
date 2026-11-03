#ifndef IR_VOXEL_EDITOR_AUTHORED_VOXEL_FINGERPRINT_H
#define IR_VOXEL_EDITOR_AUTHORED_VOXEL_FINGERPRINT_H

#include <irreden/voxel/components/component_voxel_set.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace IRVoxelEditor {

struct AuthoredVoxelFingerprint {
    std::uint64_t hash_ = 14695981039346656037ull;
    std::size_t active_ = 0;
    bool operator==(const AuthoredVoxelFingerprint &) const = default;
};

inline AuthoredVoxelFingerprint
authoredVoxelFingerprint(std::span<const IRComponents::C_Voxel> records) {
    AuthoredVoxelFingerprint result;
    const auto byte = [&](std::uint8_t value) {
        result.hash_ ^= value;
        result.hash_ *= 1099511628211ull;
    };
    for (const auto &voxel : records) {
        result.active_ += voxel.color_.alpha_ != 0;
        byte(voxel.color_.red_);
        byte(voxel.color_.green_);
        byte(voxel.color_.blue_);
        byte(voxel.color_.alpha_);
        byte(voxel.material_id_);
        byte(voxel.bone_id_);
        byte(voxel.layer_id_);
        for (int shift = 0; shift < 32; shift += 8)
            byte(static_cast<std::uint8_t>(voxel.reserved_ >> shift));
    }
    return result;
}

inline AuthoredVoxelFingerprint authoredVoxelFingerprint(const IRComponents::C_VoxelSetNew &set) {
    return authoredVoxelFingerprint(set.authoredRecords());
}

} // namespace IRVoxelEditor

#endif
