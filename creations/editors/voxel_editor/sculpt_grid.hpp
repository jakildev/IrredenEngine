#ifndef IR_VOXEL_EDITOR_SCULPT_GRID_H
#define IR_VOXEL_EDITOR_SCULPT_GRID_H

#include <irreden/ir_math.hpp>

#include <optional>
#include <cstdint>
#include <vector>

namespace IRVoxelEditor {

inline bool cellInBounds(IRMath::ivec3 cell, IRMath::ivec3 size) {
    return cell.x >= 0 && cell.y >= 0 && cell.z >= 0 && cell.x < size.x && cell.y < size.y &&
           cell.z < size.z;
}

// Coordinates are local to the voxel set. A stroke keeps the press plane so
// its own newly placed voxels cannot pull subsequent samples toward the camera.
inline std::optional<IRMath::ivec3> pickSculptPlane(
    IRMath::vec3 rayOrigin,
    IRMath::vec3 rayDirection,
    IRMath::ivec3 size,
    int axis,
    int slice,
    float planeOffset = 0.0f
) {
    if (axis < 0 || axis > 2 || slice < 0 || slice >= size[axis] ||
        IRMath::abs(rayDirection[axis]) < 1e-6f) {
        return std::nullopt;
    }
    const float distance =
        (static_cast<float>(slice) + planeOffset - rayOrigin[axis]) / rayDirection[axis];
    IRMath::ivec3 cell = IRMath::roundVec3HalfUp(rayOrigin + distance * rayDirection);
    cell[axis] = slice;
    return cellInBounds(cell, size) ? std::optional{cell} : std::nullopt;
}

// An empty route to an in-plane edge escapes the outline. Collect before
// editing so a leaking region never leaves a partially filled slice.
template <typename IsOccupied>
std::vector<IRMath::ivec3> enclosedPlaneCells(
    IRMath::ivec3 seed, IRMath::ivec3 size, int normalAxis, IsOccupied &&isOccupied
) {
    if (normalAxis < 0 || normalAxis > 2 || !cellInBounds(seed, size))
        return {};
    const int u = (normalAxis + 1) % 3;
    const int v = (normalAxis + 2) % 3;
    const auto onEdge = [&](IRMath::ivec3 cell) {
        return cell[u] == 0 || cell[v] == 0 || cell[u] == size[u] - 1 || cell[v] == size[v] - 1;
    };
    if (onEdge(seed) || isOccupied(seed))
        return {};
    // Every four-connected enclosure blocks each straight escape ray. This
    // rejects blank candidate slices without allocating a plane-sized visit map.
    for (int axis : {u, v}) {
        for (int step : {-1, 1}) {
            auto probe = seed;
            do {
                probe[axis] += step;
                if (isOccupied(probe))
                    break;
                if (onEdge(probe))
                    return {};
            } while (true);
        }
    }
    const auto index = [&](IRMath::ivec3 cell) {
        return static_cast<std::size_t>(cell[v]) * size[u] + cell[u];
    };
    std::vector<bool> visited(static_cast<std::size_t>(size[u]) * size[v]);
    std::vector<IRMath::ivec3> cells{seed};
    visited[index(seed)] = true;
    for (std::size_t next = 0; next < cells.size(); ++next) {
        const auto cell = cells[next];
        for (int axis : {u, v}) {
            for (int step : {-1, 1}) {
                auto neighbor = cell;
                neighbor[axis] += step;
                const auto slot = index(neighbor);
                if (visited[slot])
                    continue;
                visited[slot] = true;
                if (isOccupied(neighbor))
                    continue;
                if (onEdge(neighbor))
                    return {};
                cells.push_back(neighbor);
            }
        }
    }
    return cells;
}

// Includes both endpoints and visits every intermediate cell even when the
// pointer crosses several cells between input frames.
template <typename Visit>
void forEachStrokeCell(IRMath::ivec3 from, IRMath::ivec3 to, Visit &&visit) {
    const IRMath::ivec3 delta = to - from;
    const int steps =
        IRMath::max(IRMath::abs(delta.x), IRMath::max(IRMath::abs(delta.y), IRMath::abs(delta.z)));
    if (steps == 0) {
        visit(from);
        return;
    }
    for (int step = 0; step <= steps; ++step) {
        IRMath::ivec3 cell{};
        for (int axis = 0; axis < 3; ++axis) {
            const std::int64_t numerator = static_cast<std::int64_t>(from[axis]) * steps +
                                           static_cast<std::int64_t>(delta[axis]) * step;
            std::int64_t whole = numerator / steps;
            std::int64_t remainder = numerator % steps;
            if (remainder < 0) {
                --whole;
                remainder += steps;
            }
            cell[axis] = static_cast<int>(whole + (2 * remainder >= steps ? 1 : 0));
        }
        visit(cell);
    }
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_SCULPT_GRID_H */
