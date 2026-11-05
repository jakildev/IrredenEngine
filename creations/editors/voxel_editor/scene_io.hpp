#ifndef SCENE_IO_H
#define SCENE_IO_H

// Save/load round-trip for the voxel editor scene.
//
// Each animation frame is persisted as a separate DENSE-mode .vxs file:
//   {dir}/{baseName}_frame_{N}.vxs
//
// Frame 0's META chunk carries all animation and layer metadata so a single
// file can seed the entire session without a separate sidecar. The META
// entries are UTF-8 key/value pairs (see engine/asset/voxel_set_format.hpp).
//
// Per-voxel layer_id_ travels in the VOXR chunk because VoxelRecord::layer_id_
// already encodes it; the META layer_N_* entries carry display names, colors,
// and visibility only.
//
// Sidecars (.vxs.json) are emitted by saveDenseVoxelSet automatically
// (Save Format Extensibility Rule #6 — regenerated from binary, never source
// of truth; the load path ignores them).

#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/asset/rig_format.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/dense_bridge.hpp>
#include <irreden/ir_math.hpp>

#include "animation.hpp"
#include "composite_save_transaction.hpp"
#include "document_lifecycle.hpp"
#include "editor_layer_manager.hpp"
#include "symmetry.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace IRVoxelEditor {

constexpr std::string_view kSceneSaveDir = "data/editor_scene";
constexpr std::string_view kSceneBaseName = "scene";

struct SaveResult {
    bool ok_ = false;
    std::string errorMsg_;
};

struct LoadResult {
    bool ok_ = false;
    std::string errorMsg_;
    std::vector<std::vector<IRComponents::C_Voxel>> frameSnapshots_;
    float fps_ = 12.0f;
    LoopMode loopMode_ = LoopMode::LOOP;
    int activeFrame_ = 0;
    SymmetryState symmetry_;
    std::vector<LayerRecord> layers_;
    std::uint8_t activeLayerId_ = 0;
    std::uint8_t nextLayerId_ = 1;
    IRMath::ivec3 gridSize_{0};
    std::optional<IRAsset::Rig> rig_;
};

inline std::string validateEditorRig(const IRAsset::Rig &rig) {
    if (!rig.bindPoints_.empty())
        return "Named documents do not support rig bind points";
    for (std::size_t index = 0; index < rig.joints_.size(); ++index) {
        const auto &joint = rig.joints_[index];
        if (joint.parentIndex_ > index && joint.parentIndex_ < rig.joints_.size())
            return "Rig parents must precede their children";
        float rotationLengthSquared = 0.0f;
        for (int axis = 0; axis < 4; ++axis) {
            if (!IRMath::isFinite(joint.rotation_[axis]) ||
                !IRMath::isFinite(joint.translation_[axis]))
                return "Rig transforms must be finite";
            rotationLengthSquared += joint.rotation_[axis] * joint.rotation_[axis];
        }
        if (!IRMath::isFinite(rotationLengthSquared) || rotationLengthSquared <= 0.0f)
            return "Rig rotations must be nonzero finite quaternions";
    }
    return {};
}

namespace detail {

// The animation's on-disk filename shape. `IRShapeDebug --load-vxs` parses it
// back (resolveVxsFramePaths in creations/demos/shape_debug/main.cpp) to load a
// whole animation from any one of its frame files, so a change here is a change
// there.
inline std::string framePath(const std::string &dir, const std::string &baseName, int frameIdx) {
    return dir + "/" + baseName + "_frame_" + std::to_string(frameIdx) + ".vxs";
}

inline IRAsset::VoxelRecord toVoxelRecord(const IRComponents::C_Voxel &v) {
    return IRPrefab::DenseVoxel::toRecord(v);
}

inline IRComponents::C_Voxel toCVoxel(const IRAsset::VoxelRecord &r) {
    return IRComponents::C_Voxel{r.color_, r.material_id_, r.flags_, r.bone_id_, r.layer_id_};
}

// Build the META entries for frame 0 — carries animation + layer metadata.
inline std::vector<IRAsset::MetaEntry> buildMeta(
    const EditorLayerManager &layerMgr, const AnimationState &anim, const SymmetryState &sym
) {
    auto entry = [](std::string k, std::string v) -> IRAsset::MetaEntry {
        return IRAsset::MetaEntry{std::move(k), std::move(v)};
    };

    std::vector<IRAsset::MetaEntry> meta;
    meta.push_back(entry("fps", std::to_string(anim.fps_)));
    meta.push_back(entry("loop_mode", anim.loopMode_ == LoopMode::LOOP ? "LOOP" : "PING_PONG"));
    meta.push_back(entry("frame_count", std::to_string(anim.frameCount())));
    meta.push_back(entry("active_frame", std::to_string(anim.activeFrame_)));

    meta.push_back(entry("sym_enable_x", sym.enableX_ ? "1" : "0"));
    meta.push_back(entry("sym_enable_y", sym.enableY_ ? "1" : "0"));
    meta.push_back(entry("sym_enable_z", sym.enableZ_ ? "1" : "0"));
    meta.push_back(entry("sym_offset_x", std::to_string(sym.offsetX_)));
    meta.push_back(entry("sym_offset_y", std::to_string(sym.offsetY_)));
    meta.push_back(entry("sym_offset_z", std::to_string(sym.offsetZ_)));

    const auto &layers = layerMgr.layers();
    meta.push_back(entry("layer_count", std::to_string(layers.size())));
    meta.push_back(entry("active_layer_id", std::to_string(layerMgr.activeLayerId())));
    meta.push_back(entry("next_layer_id", std::to_string(layerMgr.nextId())));

    for (std::size_t i = 0; i < layers.size(); ++i) {
        const auto &layer = layers[i];
        const std::string prefix = "layer_" + std::to_string(i) + "_";
        meta.push_back(entry(prefix + "id", std::to_string(layer.id_)));
        meta.push_back(entry(prefix + "name", layer.name_));
        meta.push_back(entry(prefix + "visible", layer.visible_ ? "1" : "0"));
        meta.push_back(entry(prefix + "color_r", std::to_string(layer.colorTag_.red_)));
        meta.push_back(entry(prefix + "color_g", std::to_string(layer.colorTag_.green_)));
        meta.push_back(entry(prefix + "color_b", std::to_string(layer.colorTag_.blue_)));
        meta.push_back(entry(prefix + "color_a", std::to_string(layer.colorTag_.alpha_)));
    }

    return meta;
}

} // namespace detail

// Save the full editor scene to {dir}/{baseName}_frame_{N}.vxs files.
// Frame 0 additionally carries animation + layer metadata in its META chunk.
// Creates the directory if it doesn't exist.
inline SaveResult saveEditorScene(
    const std::string &dir,
    const std::string &baseName,
    const std::vector<std::vector<IRComponents::C_Voxel>> &frameVoxelSnapshots,
    const IRMath::ivec3 &gridSize,
    const EditorLayerManager &layerMgr,
    const AnimationState &anim,
    const SymmetryState &sym,
    const IRAsset::Rig *rig = nullptr
) {
    if (rig) {
        const auto error = validateEditorRig(*rig);
        if (!error.empty())
            return {false, error};
    }
    if (!DocumentLocation{dir, baseName}.valid())
        return {false, "Document directory and name are invalid"};
    if (frameVoxelSnapshots.empty() || frameVoxelSnapshots.size() != anim.frames_.size() ||
        frameVoxelSnapshots.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        anim.activeFrame_ < 0 || anim.activeFrame_ >= anim.frameCount() ||
        !IRMath::isFinite(anim.fps_) || anim.fps_ <= 0.0f || !IRMath::isFinite(sym.offsetX_) ||
        !IRMath::isFinite(sym.offsetY_) || !IRMath::isFinite(sym.offsetZ_))
        return {false, "Invalid animation metadata"};
    std::size_t voxelCount = 1;
    for (int axis = 0; axis < 3; ++axis) {
        if (gridSize[axis] <= 0 || voxelCount > std::numeric_limits<std::size_t>::max() /
                                                    static_cast<std::size_t>(gridSize[axis]))
            return {false, "Invalid voxel grid dimensions"};
        voxelCount *= static_cast<std::size_t>(gridSize[axis]);
    }
    std::array<bool, 256> layerIds{};
    for (const LayerRecord &layer : layerMgr.layers()) {
        if (layerIds[layer.id_] || (layerMgr.nextId() != 0 && layer.id_ >= layerMgr.nextId()))
            return {false, "Invalid layer ids"};
        layerIds[layer.id_] = true;
    }
    if (!layerIds[0] || !layerIds[layerMgr.activeLayerId()])
        return {false, "Missing default or active layer"};
    for (const auto &frame : frameVoxelSnapshots) {
        if (frame.size() != voxelCount)
            return {false, "Frame voxel count does not match grid dimensions"};
    }

    std::error_code error;
    std::filesystem::create_directories(dir, error);
    if (error)
        return {false, "Could not create directory '" + dir + "': " + error.message()};
    CompositeSaveTransaction transaction;
    if (!transaction.begin(dir, baseName))
        return {false, transaction.error()};

    if (rig) {
        const auto path = transaction.stageFile(baseName + ".rig");
        if (!path)
            return {false, transaction.error()};
        const auto written = IRAsset::saveRig(baseName, path->parent_path().string(), *rig);
        if (!written.ok())
            return {false, "Could not stage rig: " + written.message_};
    }
    const int frameCount = static_cast<int>(frameVoxelSnapshots.size());
    // Frame zero publishes last because its metadata declares the complete bundle.
    for (int ordinal = 0; ordinal < frameCount; ++ordinal) {
        const int frameIndex = (ordinal + 1) % frameCount;
        const std::string filename = baseName + "_frame_" + std::to_string(frameIndex) + ".vxs";
        const auto path = transaction.stageFile(filename);
        if (!path)
            return {false, transaction.error()};
        IRAsset::DenseVoxelSet dense;
        dense.boundsMin_ = IRMath::ivec3(0);
        dense.boundsMax_ = gridSize;
        dense.voxels_.reserve(voxelCount);
        for (const auto &voxel : frameVoxelSnapshots[frameIndex])
            dense.voxels_.push_back(detail::toVoxelRecord(voxel));
        if (frameIndex == 0) {
            dense.meta_ = detail::buildMeta(layerMgr, anim, sym);
            dense.meta_.push_back({"has_rig", rig ? "1" : "0"});
        }
        if (!IRAsset::saveDenseVoxelSet(path->string(), dense).ok())
            return {false, "Failed to stage frame " + std::to_string(frameIndex)};
    }
    if (!transaction.publish())
        return {false, transaction.error()};
    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        transaction.publishOptionalFile(
            baseName + "_frame_" + std::to_string(frameIndex) + ".vxs.json"
        );
    if (rig)
        transaction.publishOptionalFile(baseName + ".rig.json");
    return {true, {}};
}

// Load a scene previously saved by saveEditorScene.
// Reads frame 0 for animation + layer metadata, then loads all frames.
inline LoadResult loadEditorScene(const std::string &dir, const std::string &baseName) {
    LoadResult result;
    if (!DocumentLocation{dir, baseName}.valid())
        return {false, "Document directory and name are invalid"};

    const std::string frame0Path = detail::framePath(dir, baseName, 0);
    auto r0 = IRAsset::loadDenseVoxelSet(frame0Path);
    if (!r0.ok()) {
        LoadResult err;
        err.errorMsg_ = "Could not load frame 0 from '" + frame0Path + "'";
        return err;
    }

    const auto &f0 = r0.value_.dense_;

    // Validate frame 0 voxel data.
    if (f0.voxels_.size() != f0.voxelCount() || f0.voxelCount() == 0) {
        LoadResult err;
        err.errorMsg_ = "Frame 0 voxel count mismatch in '" + frame0Path + "'";
        return err;
    }

    result.gridSize_ = f0.boundsMax_ - f0.boundsMin_;

    // Parse META from frame 0.
    const auto &meta = f0.meta_;
    std::unordered_map<std::string, std::string> metaIndex;
    metaIndex.reserve(meta.size());
    for (const auto &e : meta) {
        if (!metaIndex.emplace(e.key_, e.value_).second)
            return {false, "Duplicate metadata key: " + e.key_};
    }
    auto get = [&](const std::string &k) -> std::string {
        auto it = metaIndex.find(k);
        return it != metaIndex.end() ? it->second : std::string{};
    };

    auto readNumber = [&](const std::string &key, auto &value) {
        const auto found = metaIndex.find(key);
        if (found == metaIndex.end())
            return true;
        const std::string &text = found->second;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
    };
    auto readByte = [&](const std::string &key, std::uint8_t &value) {
        int parsed = value;
        if (!readNumber(key, parsed) || parsed < 0 || parsed > 255)
            return false;
        value = static_cast<std::uint8_t>(parsed);
        return true;
    };
    auto readBool = [&](const std::string &key, bool &value) {
        int parsed = value ? 1 : 0;
        if (!readNumber(key, parsed) || (parsed != 0 && parsed != 1))
            return false;
        value = parsed != 0;
        return true;
    };
    int frameCount = 1;
    int layerCount = 0;
    if (!readNumber("fps", result.fps_) || !IRMath::isFinite(result.fps_) || result.fps_ <= 0.0f ||
        !readNumber("frame_count", frameCount) || frameCount <= 0 ||
        !readNumber("active_frame", result.activeFrame_) || result.activeFrame_ < 0 ||
        result.activeFrame_ >= frameCount || !readBool("sym_enable_x", result.symmetry_.enableX_) ||
        !readBool("sym_enable_y", result.symmetry_.enableY_) ||
        !readBool("sym_enable_z", result.symmetry_.enableZ_) ||
        !readNumber("sym_offset_x", result.symmetry_.offsetX_) ||
        !readNumber("sym_offset_y", result.symmetry_.offsetY_) ||
        !readNumber("sym_offset_z", result.symmetry_.offsetZ_) ||
        !IRMath::isFinite(result.symmetry_.offsetX_) ||
        !IRMath::isFinite(result.symmetry_.offsetY_) ||
        !IRMath::isFinite(result.symmetry_.offsetZ_) ||
        !readByte("active_layer_id", result.activeLayerId_) ||
        !readByte("next_layer_id", result.nextLayerId_) || !readNumber("layer_count", layerCount) ||
        layerCount < 0 || layerCount > 256) {
        return {false, "Invalid animation or layer metadata"};
    }
    const std::string loopMode = get("loop_mode");
    if (!loopMode.empty() && loopMode != "LOOP" && loopMode != "PING_PONG")
        return {false, "Invalid animation loop mode"};
    result.loopMode_ = loopMode == "PING_PONG" ? LoopMode::PING_PONG : LoopMode::LOOP;
    std::array<bool, 256> layerIds{};
    for (int index = 0; index < layerCount; ++index) {
        const std::string prefix = "layer_" + std::to_string(index) + "_";
        LayerRecord layer{0, get(prefix + "name"), true, IRMath::Color{180, 180, 200, 255}};
        if (!metaIndex.contains(prefix + "id") || !readByte(prefix + "id", layer.id_) ||
            layerIds[layer.id_] || !readBool(prefix + "visible", layer.visible_) ||
            !readByte(prefix + "color_r", layer.colorTag_.red_) ||
            !readByte(prefix + "color_g", layer.colorTag_.green_) ||
            !readByte(prefix + "color_b", layer.colorTag_.blue_) ||
            !readByte(prefix + "color_a", layer.colorTag_.alpha_))
            return {false, "Invalid or duplicate layer metadata"};
        if (result.nextLayerId_ != 0 && layer.id_ >= result.nextLayerId_)
            return {false, "Next layer id would reuse an existing id"};
        layerIds[layer.id_] = true;
        result.layers_.push_back(std::move(layer));
    }
    if (layerCount == 0) {
        if (metaIndex.contains("layer_count") || result.activeLayerId_ != 0 ||
            result.nextLayerId_ != 1)
            return {false, "Document must contain a default layer"};
        result.layers_ = EditorLayerManager{}.layers();
        layerIds[0] = true;
    }
    if (!layerIds[0] || !layerIds[result.activeLayerId_] ||
        (result.nextLayerId_ != 0 && layerIds[result.nextLayerId_]))
        return {false, "Invalid active or next layer id"};

    auto convertVoxels = [&](const IRAsset::DenseVoxelSet &dense,
                             std::vector<IRComponents::C_Voxel> &out) {
        out.reserve(dense.voxels_.size());
        for (const auto &r : dense.voxels_)
            out.push_back(detail::toCVoxel(r));
    };

    result.frameSnapshots_.emplace_back();
    convertVoxels(f0, result.frameSnapshots_.back());

    for (int fi = 1; fi < frameCount; ++fi) {
        const std::string path = detail::framePath(dir, baseName, fi);
        auto ri = IRAsset::loadDenseVoxelSet(path);
        if (!ri.ok()) {
            LoadResult err;
            err.errorMsg_ = "Could not load frame " + std::to_string(fi) + " from '" + path + "'";
            return err;
        }
        const auto &fd = ri.value_.dense_;
        if (fd.voxels_.size() != fd.voxelCount() || fd.boundsMin_ != f0.boundsMin_ ||
            fd.boundsMax_ != f0.boundsMax_) {
            LoadResult err;
            err.errorMsg_ = "Frame " + std::to_string(fi) + " voxel count mismatch";
            return err;
        }
        result.frameSnapshots_.emplace_back();
        convertVoxels(fd, result.frameSnapshots_.back());
    }

    bool hasRig = false;
    if (!readBool("has_rig", hasRig))
        return {false, "Invalid has_rig metadata"};
    if (hasRig) {
        auto loadedRig = IRAsset::loadRig(baseName, dir);
        if (!loadedRig.ok())
            return {false, "Could not load document rig: " + loadedRig.status_.message_};
        const auto error = validateEditorRig(loadedRig.value_);
        if (!error.empty())
            return {false, error};
        result.rig_ = std::move(loadedRig.value_);
    }
    result.ok_ = true;
    return result;
}

} // namespace IRVoxelEditor

#endif
