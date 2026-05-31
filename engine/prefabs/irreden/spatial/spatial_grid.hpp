#ifndef IR_SPATIAL_GRID_H
#define IR_SPATIAL_GRID_H

// PURPOSE: World-3D uniform cell hash for batched neighbour queries. Each
//   queryable entity is inserted at a single point (its world translation)
//   once per frame; consumers query by radius or AABB and receive a
//   contiguous vector of {EntityId, position} records. Returning the
//   position INLINE is the whole point — it lets the Lua/gameplay caller
//   resolve neighbours without a per-candidate foreign component read (the
//   getLuaField-per-pair footgun). See
//   docs/design/lua-world-space-neighbour-query.md for the locked contract.
//
// ALLOCATION DISCIPLINE (Pattern B): queryRadius/queryAabb write into a
//   CALLER-OWNED out vector and never allocate a temporary set/vector per
//   query (contrast iso_spatial_hash.hpp, the render-cull index, which
//   allocates an unordered_set + vector on every query). Build reuses bucket
//   capacity across frames: clear() empties only the cells touched last frame
//   (tracked in m_touchedKeys) without freeing their vectors, and map nodes
//   are never erased — so after a short warm-up the build + query hot paths
//   perform zero heap allocation.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace IRPrefab::Spatial {

// One neighbour result. `pos_` is the world translation captured at insert
// time, so the caller never needs a foreign C_WorldTransform read to learn a
// neighbour's position.
struct SpatialHit {
    IREntity::EntityId id_;
    IRMath::vec3 pos_;
};

class SpatialGrid {
  public:
    // Default-constructs at the 64-unit cell size (the value embedded as a
    // singleton in C_SpatialIndex). The default ctor stays non-explicit so
    // C_SpatialIndex — an aggregate — can value-initialize its grid_ member.
    SpatialGrid() = default;

    // `cellSize` should be sized to the dominant query radius (a query scans
    // every cell its sphere/box overlaps, so cells much smaller than the
    // radius scan many empty buckets; cells much larger gather many distant
    // entities the geometric filter then rejects).
    explicit SpatialGrid(float cellSize)
        : m_cellSize{cellSize} {}

    // Drop last frame's contents while keeping bucket + scratch capacity, so
    // the following frame's inserts and queries allocate nothing once the
    // working set has been seen. O(cells touched last frame), not O(world).
    void clear() {
        for (std::int64_t key : m_touchedKeys) {
            m_cells[key].clear(); // keep the bucket's vector capacity
        }
        m_touchedKeys.clear(); // keep the touched-key vector's capacity
    }

    // Insert an entity at a single world point. Call once per queryable
    // entity per frame (between clear() and the first query).
    void insert(IREntity::EntityId entity, IRMath::vec3 worldPos) {
        const std::int64_t key = cellKey(cellOf(worldPos));
        std::vector<SpatialHit> &bucket = m_cells[key];
        if (bucket.empty()) {
            // First insert into this cell this frame — remember it so next
            // frame's clear() can empty exactly the cells we used.
            m_touchedKeys.push_back(key);
        }
        bucket.push_back(SpatialHit{entity, worldPos});
    }

    // Gather every inserted entity within `radius` of `center`. Clears `out`
    // then appends the hits; pass a reused scratch vector to stay alloc-free.
    void queryRadius(IRMath::vec3 center, float radius, std::vector<SpatialHit> &out) const {
        out.clear();
        if (radius <= 0.0f) {
            return;
        }
        const float radiusSq = radius * radius;
        const IRMath::vec3 extent(radius);
        const IRMath::ivec3 lo = cellOf(center - extent);
        const IRMath::ivec3 hi = cellOf(center + extent);
        for (int z = lo.z; z <= hi.z; ++z) {
            for (int y = lo.y; y <= hi.y; ++y) {
                for (int x = lo.x; x <= hi.x; ++x) {
                    auto it = m_cells.find(cellKey(IRMath::ivec3(x, y, z)));
                    if (it == m_cells.end()) {
                        continue;
                    }
                    for (const SpatialHit &hit : it->second) {
                        const IRMath::vec3 delta = hit.pos_ - center;
                        if (IRMath::dot(delta, delta) <= radiusSq) {
                            out.push_back(hit);
                        }
                    }
                }
            }
        }
    }

    // Gather every inserted entity inside the inclusive AABB [min, max].
    // Clears `out` then appends; same caller-owns-capacity contract.
    void queryAabb(IRMath::vec3 min, IRMath::vec3 max, std::vector<SpatialHit> &out) const {
        out.clear();
        const IRMath::ivec3 lo = cellOf(min);
        const IRMath::ivec3 hi = cellOf(max);
        for (int z = lo.z; z <= hi.z; ++z) {
            for (int y = lo.y; y <= hi.y; ++y) {
                for (int x = lo.x; x <= hi.x; ++x) {
                    auto it = m_cells.find(cellKey(IRMath::ivec3(x, y, z)));
                    if (it == m_cells.end()) {
                        continue;
                    }
                    for (const SpatialHit &hit : it->second) {
                        const IRMath::vec3 &p = hit.pos_;
                        if (p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y &&
                            p.z >= min.z && p.z <= max.z) {
                            out.push_back(hit);
                        }
                    }
                }
            }
        }
    }

  private:
    float m_cellSize = 64.0f;
    std::unordered_map<std::int64_t, std::vector<SpatialHit>> m_cells;
    // Keys inserted-into this frame (deduped: pushed only on a cell's first
    // insert). clear() walks this list instead of the whole map so its cost
    // tracks the live population, not every cell ever visited.
    std::vector<std::int64_t> m_touchedKeys;

    IRMath::ivec3 cellOf(IRMath::vec3 p) const {
        return IRMath::ivec3(
            static_cast<int>(IRMath::floor(p.x / m_cellSize)),
            static_cast<int>(IRMath::floor(p.y / m_cellSize)),
            static_cast<int>(IRMath::floor(p.z / m_cellSize))
        );
    }

    // Pack a cell coordinate into one 64-bit key, 21 signed bits per axis
    // (range ±2^20 cells ≈ ±67M world units at the default cell size).
    // Distinct in-range cells map to distinct keys. Coordinates outside the
    // range fold (wrap), which only ever merges two cells into one bucket —
    // the geometric filter in queryRadius/queryAabb still rejects the
    // out-of-range entities, so a fold costs a little extra scanning, never
    // a wrong result.
    static std::int64_t cellKey(IRMath::ivec3 cell) {
        constexpr std::int64_t kBitsPerAxis = 21; // 3 axes × 21 = 63 ≤ 64
        constexpr std::int64_t kMask = (static_cast<std::int64_t>(1) << kBitsPerAxis) - 1;
        return (static_cast<std::int64_t>(cell.x) & kMask) |
               ((static_cast<std::int64_t>(cell.y) & kMask) << kBitsPerAxis) |
               ((static_cast<std::int64_t>(cell.z) & kMask) << (2 * kBitsPerAxis));
    }
};

} // namespace IRPrefab::Spatial

#endif /* IR_SPATIAL_GRID_H */
