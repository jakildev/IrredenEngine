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
    int rotationalOrder_ = 0;
    IRMath::vec3 rotationalAxis_ = IRMath::vec3(0.0f, 0.0f, 1.0f);
};

// The mirror-plane offset that reflects within a [0, sizeAxis) cell range: the
// scene centre, pairing cell 0 with sizeAxis-1. There is no UI for an off-centre
// plane, so the centre is the only meaningful position — the editor's X/Y/Z
// toggles and the session builder both seat an enabled axis here (via this
// helper) so their reflections agree on where a mirrored voxel lands.
inline float mirrorCenterOffset(int sizeAxis) {
    return (sizeAxis - 1) * 0.5f;
}

// Seats mirror axis @p axis (0 x, 1 y, 2 z) at the centre of a set of @p size.
// The offset is per set: one seated for a different extent reflects cells out
// of bounds.
inline void seatMirrorAxis(SymmetryState &sym, int axis, IRMath::ivec3 size) {
    const float offset = mirrorCenterOffset(size[axis]);
    if (axis == 0) {
        sym.offsetX_ = offset;
    } else if (axis == 1) {
        sym.offsetY_ = offset;
    } else {
        sym.offsetZ_ = offset;
    }
}

// Seats every enabled axis for a set of @p size: what a change of editable set
// owes the mirror state.
inline void seatEnabledMirrorAxes(SymmetryState &sym, IRMath::ivec3 size) {
    if (sym.enableX_) {
        seatMirrorAxis(sym, 0, size);
    }
    if (sym.enableY_) {
        seatMirrorAxis(sym, 1, size);
    }
    if (sym.enableZ_) {
        seatMirrorAxis(sym, 2, size);
    }
}

// How far a seated plane may sit from another and still be the same plane.
// Planes land on multiples of 0.5, so this only absorbs float noise.
inline constexpr float kMirrorOffsetTolerance = 0.001f;

// Whether two states mirror identically: the same enabled axes, each seated at
// the same plane. A disabled axis's offset is not compared.
inline bool mirrorPlanesMatch(const SymmetryState &a, const SymmetryState &b) {
    const auto axisMatches = [](bool enabledA, float offsetA, bool enabledB, float offsetB) {
        return enabledA == enabledB &&
               (!enabledA || IRMath::abs(offsetA - offsetB) <= kMirrorOffsetTolerance);
    };
    return axisMatches(a.enableX_, a.offsetX_, b.enableX_, b.offsetX_) &&
           axisMatches(a.enableY_, a.offsetY_, b.enableY_, b.offsetY_) &&
           axisMatches(a.enableZ_, a.offsetZ_, b.enableZ_, b.offsetZ_);
}

// Fills `out` with all positions (including `pos`) where a voxel should be placed
// or erased. For each enabled mirror axis, every accumulated position is reflected;
// processing order is X → Y → Z (XYZ-active gives up to 8 positions). Positions
// that reflect back onto themselves are not duplicated.
// `out` is cleared on entry; caller retains the buffer across strokes to amortize
// allocations.
inline void
applyMirrors(IRMath::ivec3 pos, const SymmetryState &sym, std::vector<IRMath::ivec3> &out) {
    using IRMath::ivec3;
    out.clear();
    out.push_back(pos);

    auto mirrorAxis = [](int v, float offset) -> int {
        return IRMath::roundHalfUp(2.0f * offset - static_cast<float>(v));
    };

    auto expand = [&](bool enabled, int axis, float offset) {
        if (!enabled)
            return;
        const size_t n = out.size();
        for (size_t i = 0; i < n; ++i) {
            ivec3 m = out[i];
            if (axis == 0)
                m.x = mirrorAxis(m.x, offset);
            else if (axis == 1)
                m.y = mirrorAxis(m.y, offset);
            else
                m.z = mirrorAxis(m.z, offset);
            if (m != out[i])
                out.push_back(m);
        }
    };

    expand(sym.enableX_, 0, sym.offsetX_);
    expand(sym.enableY_, 1, sym.offsetY_);
    expand(sym.enableZ_, 2, sym.offsetZ_);
}

inline IRMath::ivec3
rotateCell(IRMath::ivec3 pos, IRMath::ivec3 size, IRMath::vec3 axis, int steps, int order) {
    const IRMath::vec3 center = (IRMath::vec3(size) - IRMath::vec3(1.0f)) * 0.5f;
    const float angle = IRMath::kTwoPi * static_cast<float>(steps) / static_cast<float>(order);
    const IRMath::vec3 rotated =
        center +
        IRMath::rotateVectorByQuat(IRMath::vec3(pos) - center, IRMath::quatAxisAngle(axis, angle));
    return IRMath::roundVec3HalfUp(rotated);
}

} // namespace IRVoxelEditor
