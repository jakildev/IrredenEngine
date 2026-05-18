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

// Fills `out` with all positions (including `pos`) where a voxel should be placed
// or erased. For each enabled mirror axis, every accumulated position is reflected;
// processing order is X → Y → Z (XYZ-active gives up to 8 positions). Positions
// that reflect back onto themselves are not duplicated.
// `out` is cleared on entry; caller retains the buffer across strokes to amortize
// allocations.
inline void applyMirrors(
    IRMath::ivec3 pos,
    const SymmetryState& sym,
    std::vector<IRMath::ivec3>& out)
{
    using IRMath::ivec3;
    out.clear();
    out.push_back(pos);

    auto mirrorAxis = [](int v, float offset) -> int {
        return IRMath::roundHalfUp(2.0f * offset - static_cast<float>(v));
    };

    auto expand = [&](bool enabled, int axis, float offset) {
        if (!enabled) return;
        const size_t n = out.size();
        for (size_t i = 0; i < n; ++i) {
            ivec3 m = out[i];
            if (axis == 0)      m.x = mirrorAxis(m.x, offset);
            else if (axis == 1) m.y = mirrorAxis(m.y, offset);
            else                m.z = mirrorAxis(m.z, offset);
            if (m != out[i])
                out.push_back(m);
        }
    };

    expand(sym.enableX_, 0, sym.offsetX_);
    expand(sym.enableY_, 1, sym.offsetY_);
    expand(sym.enableZ_, 2, sym.offsetZ_);
}

} // namespace IRVoxelEditor
