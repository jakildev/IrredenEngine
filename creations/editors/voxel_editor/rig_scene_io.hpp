#pragma once

// Save/load round-trip for the editor's entity-based skeleton.
//
// Ctrl+Shift+S: skeletonToRig() packs C_Skeleton joint transforms into an
//   IRAsset::Rig (JNTS chunk), then writes to {dir}/{baseName}.rig.
// Ctrl+Shift+O: loads the .rig as an IRAsset::Rig; the caller reconstructs
//   joint entities, CHILD_OF relations, C_Skeleton, and bindPose_.
//
// Only the JNTS chunk is written — BIND (named attachment points managed
// by C_BindPoints) is a separate concept outside the joint-authoring tool.
//
// parentIndex sentinel: joints parented to the rig root are stored on disk
// with parentIndex == their own joint index (self-reference).  The load
// path in main.cpp maps self-ref or out-of-range back to parentIdx = -1.

#include <irreden/asset/rig_format.hpp>

#include <irreden/voxel/components/component_skeleton.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace IRVoxelEditor {

struct RigSaveResult {
    bool ok_ = false;
    std::string errorMsg_;
};

struct RigLoadResult {
    bool ok_ = false;
    std::string errorMsg_;
    IRAsset::Rig rig_;
};

// Build an IRAsset::Rig from the live entity skeleton.
// parentIdx is parallel to C_Skeleton.joints_; -1 means the joint's parent
// is the rig root, stored on disk as a self-reference sentinel.
inline IRAsset::Rig skeletonToRig(
    IREntity::EntityId rigRoot,
    const std::vector<int> &parentIdx
) {
    const auto &skeleton =
        IREntity::getComponent<IRComponents::C_Skeleton>(rigRoot);
    const std::size_t count = skeleton.joints_.size();
    IR_ASSERT(parentIdx.size() == count, "parentIdx / skeleton.joints_ size mismatch in skeletonToRig");

    IRAsset::Rig rig;
    rig.joints_.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const auto &xform =
            IREntity::getComponent<IRComponents::C_LocalTransform>(skeleton.joints_[i]);
        IRAsset::RigJoint j;
        j.rotation_ = xform.rotation_;
        j.translation_ = IRMath::vec4{
            xform.translation_.x, xform.translation_.y, xform.translation_.z, 0.0f};
        j.parentIndex_ = (parentIdx[i] < 0)
            ? static_cast<std::uint32_t>(i)
            : static_cast<std::uint32_t>(parentIdx[i]);
        rig.joints_.push_back(std::move(j));
    }

    return rig;
}

// Save the editor skeleton to {dir}/{baseName}.rig.
// Creates the directory if it does not exist.
inline RigSaveResult saveRigScene(
    const std::string &dir,
    const std::string &baseName,
    IREntity::EntityId rigRoot,
    const std::vector<int> &parentIdx
) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        RigSaveResult err;
        err.errorMsg_ = "Could not create directory '" + dir + "': " + ec.message();
        return err;
    }

    const IRAsset::Rig rig = skeletonToRig(rigRoot, parentIdx);
    const IRAsset::BinaryStatus status = IRAsset::saveRig(baseName, dir, rig);
    if (!status.ok()) {
        RigSaveResult err;
        err.errorMsg_ = "saveRig failed for '" + dir + "/" + baseName + ".rig'";
        return err;
    }

    RigSaveResult success;
    success.ok_ = true;
    return success;
}

// Load a rig previously saved by saveRigScene.
// Returns the asset-level IRAsset::Rig; the caller reconstructs joint entities.
inline RigLoadResult loadRigScene(const std::string &dir, const std::string &baseName) {
    auto r = IRAsset::loadRig(baseName, dir);
    if (!r.ok()) {
        RigLoadResult err;
        err.errorMsg_ = "Could not load rig from '" + dir + "/" + baseName + ".rig'";
        return err;
    }

    RigLoadResult result;
    result.ok_ = true;
    result.rig_ = std::move(r.value_);
    return result;
}

} // namespace IRVoxelEditor
