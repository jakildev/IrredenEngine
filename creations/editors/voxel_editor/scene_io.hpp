#pragma once

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
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/ir_math.hpp>

#include "animation.hpp"
#include "editor_layer_manager.hpp"
#include "symmetry.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
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
};

namespace detail {

inline std::string framePath(const std::string &dir, const std::string &baseName, int frameIdx) {
    return dir + "/" + baseName + "_frame_" + std::to_string(frameIdx) + ".vxs";
}

inline IRAsset::VoxelRecord toVoxelRecord(const IRComponents::C_Voxel &v) {
    IRAsset::VoxelRecord r;
    r.color_ = v.color_;
    r.material_id_ = v.material_id_;
    r.flags_ = v.flags_;
    r.bone_id_ = v.bone_id_;
    r.layer_id_ = v.layer_id_;

    return r;
}

inline IRComponents::C_Voxel toCVoxel(const IRAsset::VoxelRecord &r) {
    return IRComponents::C_Voxel{r.color_, r.material_id_, r.flags_, r.bone_id_, r.layer_id_};
}

// Build the META entries for frame 0 — carries animation + layer metadata.
inline std::vector<IRAsset::MetaEntry> buildMeta(
    const EditorLayerManager &layerMgr,
    const AnimationState &anim,
    const SymmetryState &sym
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

// Look up a key in the META entries. Returns empty string if not found.
inline std::string metaGet(const std::vector<IRAsset::MetaEntry> &meta, const std::string &key) {
    for (const auto &e : meta)
        if (e.key_ == key)
            return e.value_;
    return {};
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
    const SymmetryState &sym
) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        SaveResult err;
        err.errorMsg_ = "Could not create directory '" + dir + "': " + ec.message();
        return err;
    }

    const int frameCount = static_cast<int>(frameVoxelSnapshots.size());
    for (int fi = 0; fi < frameCount; ++fi) {
        const auto &voxels = frameVoxelSnapshots[fi];

        IRAsset::DenseVoxelSet dense;
        dense.boundsMin_ = IRMath::ivec3(0);
        dense.boundsMax_ = gridSize;
        dense.voxels_.reserve(voxels.size());
        for (const auto &v : voxels)
            dense.voxels_.push_back(detail::toVoxelRecord(v));

        if (fi == 0)
            dense.meta_ = detail::buildMeta(layerMgr, anim, sym);

        const std::string path = detail::framePath(dir, baseName, fi);
        IRAsset::BinaryStatus status = IRAsset::saveDenseVoxelSet(path, dense);
        if (!status.ok()) {
            SaveResult err;
            err.errorMsg_ = "Failed to save frame " + std::to_string(fi) + " to '" + path + "'";
            return err;
        }
    }

    SaveResult success;
    success.ok_ = true;
    return success;
}

// Load a scene previously saved by saveEditorScene.
// Reads frame 0 for animation + layer metadata, then loads all frames.
inline LoadResult loadEditorScene(const std::string &dir, const std::string &baseName) {
    LoadResult result;

    const std::string frame0Path = detail::framePath(dir, baseName, 0);
    auto r0 = IRAsset::loadDenseVoxelSet(frame0Path);
    if (!r0.ok()) {
        LoadResult err;
        err.errorMsg_ = "Could not load frame 0 from '" + frame0Path + "'";
        return err;
    }

    const auto &f0 = r0.value_.dense_;

    // Validate frame 0 voxel data.
    if (f0.voxels_.size() != f0.voxelCount()) {
        LoadResult err;
        err.errorMsg_ = "Frame 0 voxel count mismatch in '" + frame0Path + "'";
        return err;
    }

    result.gridSize_ = f0.boundsMax_ - f0.boundsMin_;

    // Parse META from frame 0.
    const auto &meta = f0.meta_;
    auto get = [&](const std::string &k) { return detail::metaGet(meta, k); };

    int frameCount = 1;
    try {
        const std::string fpsStr = get("fps");
        if (!fpsStr.empty())
            result.fps_ = std::stof(fpsStr);

        result.loopMode_ = (get("loop_mode") == "PING_PONG") ? LoopMode::PING_PONG : LoopMode::LOOP;

        const std::string fcStr = get("frame_count");
        if (!fcStr.empty())
            frameCount = std::stoi(fcStr);

        const std::string afStr = get("active_frame");
        if (!afStr.empty())
            result.activeFrame_ = std::stoi(afStr);

        const std::string sxStr = get("sym_enable_x");
        if (!sxStr.empty()) result.symmetry_.enableX_ = sxStr == "1";
        const std::string syStr = get("sym_enable_y");
        if (!syStr.empty()) result.symmetry_.enableY_ = syStr == "1";
        const std::string szStr = get("sym_enable_z");
        if (!szStr.empty()) result.symmetry_.enableZ_ = szStr == "1";

        const std::string oxStr = get("sym_offset_x");
        if (!oxStr.empty()) result.symmetry_.offsetX_ = std::stof(oxStr);
        const std::string oyStr = get("sym_offset_y");
        if (!oyStr.empty()) result.symmetry_.offsetY_ = std::stof(oyStr);
        const std::string ozStr = get("sym_offset_z");
        if (!ozStr.empty()) result.symmetry_.offsetZ_ = std::stof(ozStr);

        const std::string activeLayerIdStr = get("active_layer_id");
        if (!activeLayerIdStr.empty())
            result.activeLayerId_ = static_cast<std::uint8_t>(std::stoi(activeLayerIdStr));

        const std::string nlStr = get("next_layer_id");
        if (!nlStr.empty())
            result.nextLayerId_ = static_cast<std::uint8_t>(std::stoi(nlStr));

        int layerCount = 0;
        const std::string layerCountStr = get("layer_count");
        if (!layerCountStr.empty())
            layerCount = std::stoi(layerCountStr);

        result.layers_.reserve(static_cast<std::size_t>(layerCount));
        for (int i = 0; i < layerCount; ++i) {
            const std::string prefix = "layer_" + std::to_string(i) + "_";
            LayerRecord rec{};
            const std::string idStr = get(prefix + "id");
            if (!idStr.empty())
                rec.id_ = static_cast<std::uint8_t>(std::stoi(idStr));
            rec.name_ = get(prefix + "name");
            rec.visible_ = (get(prefix + "visible") != "0");
            const std::string cr = get(prefix + "color_r");
            const std::string cg = get(prefix + "color_g");
            const std::string cb = get(prefix + "color_b");
            const std::string ca = get(prefix + "color_a");
            rec.colorTag_.red_   = cr.empty() ? 180 : static_cast<std::uint8_t>(std::stoi(cr));
            rec.colorTag_.green_ = cg.empty() ? 180 : static_cast<std::uint8_t>(std::stoi(cg));
            rec.colorTag_.blue_  = cb.empty() ? 200 : static_cast<std::uint8_t>(std::stoi(cb));
            rec.colorTag_.alpha_ = ca.empty() ? 255 : static_cast<std::uint8_t>(std::stoi(ca));
            result.layers_.push_back(rec);
        }
    } catch (const std::invalid_argument &e) {
        LoadResult err;
        err.errorMsg_ = std::string("META parse error (invalid value): ") + e.what();
        return err;
    } catch (const std::out_of_range &e) {
        LoadResult err;
        err.errorMsg_ = std::string("META parse error (value out of range): ") + e.what();
        return err;
    }

    // Load all frames (frame 0 already in hand).
    result.frameSnapshots_.resize(static_cast<std::size_t>(frameCount));

    auto convertVoxels = [&](const IRAsset::DenseVoxelSet &dense,
                              std::vector<IRComponents::C_Voxel> &out) {
        out.reserve(dense.voxels_.size());
        for (const auto &r : dense.voxels_)
            out.push_back(detail::toCVoxel(r));
    };

    convertVoxels(f0, result.frameSnapshots_[0]);

    for (int fi = 1; fi < frameCount; ++fi) {
        const std::string path = detail::framePath(dir, baseName, fi);
        auto ri = IRAsset::loadDenseVoxelSet(path);
        if (!ri.ok()) {
            LoadResult err;
            err.errorMsg_ = "Could not load frame " + std::to_string(fi) + " from '" + path + "'";
            return err;
        }
        const auto &fd = ri.value_.dense_;
        if (fd.voxels_.size() != fd.voxelCount()) {
            LoadResult err;
            err.errorMsg_ = "Frame " + std::to_string(fi) + " voxel count mismatch";
            return err;
        }
        convertVoxels(fd, result.frameSnapshots_[fi]);
    }

    result.ok_ = true;
    return result;
}

} // namespace IRVoxelEditor
