#pragma once

#include <irreden/ir_math.hpp>
#include <vector>

namespace IRVoxelEditor {

struct SymmetryState {
    bool enableX_ = false;
    bool enableY_ = false;
    bool enableZ_ = false;
    // Plane position in voxel units per axis. 0 = mirror at origin; 0.5 = between cells 0 and -1.
    float offsetX_ = 0.0f;
    float offsetY_ = 0.0f;
    float offsetZ_ = 0.0f;
};

// Returns all positions (including `pos`) where a voxel should be placed or erased.
// For each enabled mirror axis, every accumulated position is reflected; processing
// order is X → Y → Z (XYZ-active gives up to 8 positions). Positions that reflect
// back onto themselves are not duplicated.
inline std::vector<IRMath::ivec3> applyMirrors(
    IRMath::ivec3 pos,
    const SymmetryState& sym)
{
    using IRMath::ivec3;
    std::vector<ivec3> positions{pos};

    auto mirrorAxis = [](int v, float offset) -> int {
        return IRMath::roundHalfUp(2.0f * offset - static_cast<float>(v));
    };

    auto expand = [&](bool enabled, int axis, float offset) {
        if (!enabled) return;
        const size_t n = positions.size();
        for (size_t i = 0; i < n; ++i) {
            ivec3 m = positions[i];
            if (axis == 0)      m.x = mirrorAxis(m.x, offset);
            else if (axis == 1) m.y = mirrorAxis(m.y, offset);
            else                m.z = mirrorAxis(m.z, offset);
            if (m != positions[i])
                positions.push_back(m);
        }
    };

    expand(sym.enableX_, 0, sym.offsetX_);
    expand(sym.enableY_, 1, sym.offsetY_);
    expand(sym.enableZ_, 2, sym.offsetZ_);

    return positions;
}

} // namespace IRVoxelEditor
