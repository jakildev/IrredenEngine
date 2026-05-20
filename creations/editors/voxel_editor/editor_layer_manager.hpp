#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <irreden/ir_math.hpp>

namespace IRVoxelEditor {

// A single named layer in the voxel editor. Layer 0 ("default") always
// exists and cannot be deleted. Layer IDs are stable identifiers — the
// display order in the layer list may change via reorder, but the id_
// never changes after creation.
struct LayerRecord {
    std::uint8_t id_;
    std::string name_;
    bool visible_;
    IRMath::Color colorTag_;
};

// Manages the layer list and active-layer state for the voxel editor.
//
// Each placed voxel carries a C_Voxel::layer_id_ that matches one of the
// records here. When a layer's visibility is toggled the caller (main.cpp)
// must iterate the scene's C_VoxelSetNew and update voxel alpha accordingly
// — that wiring lives in T-211 once the scene entity is instantiated.
class EditorLayerManager {
  public:
    EditorLayerManager() {
        m_layers.push_back(LayerRecord{0, "default", true, IRMath::Color{180, 180, 200, 255}});
    }

    std::uint8_t activeLayerId() const {
        return m_activeLayerId;
    }

    std::uint8_t nextId() const {
        return m_nextId;
    }

    // Discard all current layers and replace with the given snapshot.
    // Used by the scene loader to restore exact layer IDs after a round-trip.
    void resetAndLoad(
        const std::vector<LayerRecord> &layers,
        std::uint8_t activeLayerId,
        std::uint8_t nextId
    ) {
        m_layers = layers;
        m_activeLayerId = activeLayerId;
        m_nextId = nextId;
    }

    void setActiveLayer(std::uint8_t id) {
        if (findLayer(id))
            m_activeLayerId = id;
    }

    // Cycles to the previous layer in display order, wrapping around.
    void selectPrevLayer() {
        if (m_layers.size() <= 1)
            return;
        auto idx = indexOfActive();
        m_activeLayerId = m_layers[idx == 0 ? m_layers.size() - 1 : idx - 1].id_;
    }

    // Cycles to the next layer in display order, wrapping around.
    void selectNextLayer() {
        if (m_layers.size() <= 1)
            return;
        auto idx = indexOfActive();
        m_activeLayerId = m_layers[(idx + 1) % m_layers.size()].id_;
    }

    // Adds a new layer and returns its id. Returns 0 on overflow (255-layer max).
    std::uint8_t
    addLayer(std::string name, IRMath::Color colorTag = IRMath::Color{180, 180, 200, 255}) {
        if (m_nextId == 0)
            return 0; // 255-layer limit reached
        std::uint8_t id = m_nextId++;
        m_layers.push_back(LayerRecord{id, std::move(name), true, colorTag});
        return id;
    }

    bool renameLayer(std::uint8_t id, std::string name) {
        auto *rec = findLayer(id);
        if (!rec)
            return false;
        rec->name_ = std::move(name);
        return true;
    }

    // Toggles visibility. Returns the new visible state, or false if not found.
    bool toggleLayerVisibility(std::uint8_t id) {
        auto *rec = findLayer(id);
        if (!rec)
            return false;
        rec->visible_ = !rec->visible_;
        return rec->visible_;
    }

    bool isVisible(std::uint8_t id) const {
        const auto *rec = findLayer(id);
        return rec && rec->visible_;
    }

    // Deletes a layer. Layer 0 cannot be deleted. Active layer falls back to 0.
    // Caller is responsible for migrating voxels with this layer_id_ to layer 0.
    bool deleteLayer(std::uint8_t id) {
        if (id == 0)
            return false;
        auto it = std::find_if(m_layers.begin(), m_layers.end(), [id](const LayerRecord &r) {
            return r.id_ == id;
        });
        if (it == m_layers.end())
            return false;
        m_layers.erase(it);
        if (m_activeLayerId == id)
            m_activeLayerId = 0;
        return true;
    }

    bool moveLayerUp(std::uint8_t id) {
        auto idx = findIndex(id);
        if (idx >= m_layers.size() || idx == 0)
            return false;
        std::swap(m_layers[idx], m_layers[idx - 1]);
        return true;
    }

    bool moveLayerDown(std::uint8_t id) {
        auto idx = findIndex(id);
        if (idx >= m_layers.size() || idx + 1 == m_layers.size())
            return false;
        std::swap(m_layers[idx], m_layers[idx + 1]);
        return true;
    }

    const std::vector<LayerRecord> &layers() const {
        return m_layers;
    }

  private:
    LayerRecord *findLayer(std::uint8_t id) {
        auto it = std::find_if(m_layers.begin(), m_layers.end(), [id](const LayerRecord &r) {
            return r.id_ == id;
        });
        return it == m_layers.end() ? nullptr : &*it;
    }

    const LayerRecord *findLayer(std::uint8_t id) const {
        auto it = std::find_if(m_layers.begin(), m_layers.end(), [id](const LayerRecord &r) {
            return r.id_ == id;
        });
        return it == m_layers.end() ? nullptr : &*it;
    }

    std::size_t findIndex(std::uint8_t id) const {
        for (std::size_t i = 0; i < m_layers.size(); ++i)
            if (m_layers[i].id_ == id)
                return i;
        return m_layers.size();
    }

    std::size_t indexOfActive() const {
        return findIndex(m_activeLayerId);
    }

    std::vector<LayerRecord> m_layers;
    std::uint8_t m_activeLayerId = 0;
    std::uint8_t m_nextId = 1;
};

} // namespace IRVoxelEditor
