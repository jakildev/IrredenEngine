#ifndef IR_FIELD_REGIONS_H
#define IR_FIELD_REGIONS_H

// PURPOSE: Connected-component labels over the free cells of an occupancy
//   field. Each field chunk is labelled locally; a union-find over the
//   (field chunk, local label) pairs on shared seams resolves the global
//   region ids, so "same region" is one label compare instead of a search.
//   See docs/design/chunked-field-placement-kit.md (D5).

#include <irreden/ir_math.hpp>
#include <irreden/spatial/chunked_field.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace IRPrefab::Spatial {

using FieldRegionId = std::uint64_t;

// Never a region: every occupied cell and every cell of an absent field chunk
// reads as this, so equality between two invalid lookups is not reachability.
constexpr FieldRegionId kInvalidFieldRegion = 0;

namespace detail {

class UnionFind {
  public:
    void reset(std::size_t count) {
        m_parent.resize(count);
        std::iota(m_parent.begin(), m_parent.end(), 0u);
        m_size.assign(count, 1u);
    }

    void reserve(std::size_t count) {
        m_parent.reserve(count);
        m_size.reserve(count);
    }

    std::uint32_t add() {
        const auto node = static_cast<std::uint32_t>(m_parent.size());
        m_parent.push_back(node);
        m_size.push_back(1u);
        return node;
    }

    std::size_t size() const {
        return m_parent.size();
    }

    std::uint32_t find(std::uint32_t node) {
        while (m_parent[node] != node) {
            m_parent[node] = m_parent[m_parent[node]];
            node = m_parent[node];
        }
        return node;
    }

    void unite(std::uint32_t a, std::uint32_t b) {
        std::uint32_t rootA = find(a);
        std::uint32_t rootB = find(b);
        if (rootA == rootB) {
            return;
        }
        if (m_size[rootA] < m_size[rootB]) {
            std::swap(rootA, rootB);
        }
        m_parent[rootB] = rootA;
        m_size[rootA] += m_size[rootB];
    }

  private:
    std::vector<std::uint32_t> m_parent;
    std::vector<std::uint32_t> m_size;
};

} // namespace detail

class FieldRegions {
  public:
    FieldRegions() = default;

    // Borrows the occupancy field's complete dirty snapshot and never
    // acknowledges it. Every change since the previous call must be in
    // `dirtyKeys`; the first call, and any dirty key whose field chunk is now
    // absent, relabel the whole present set instead.
    void update(
        const ChunkedField2D<std::uint8_t> &occupancy, std::span<const FieldChunkKey> dirtyKeys
    ) {
        bool reset = !m_initialized;
        for (FieldChunkKey key : dirtyKeys) {
            if (occupancy.findChunk(unpackFieldChunkKey(key)) == nullptr) {
                reset = true;
                break;
            }
        }

        if (reset) {
            rebuild(occupancy);
            return;
        }
        if (dirtyKeys.empty()) {
            return;
        }
        m_workKeys.assign(dirtyKeys.begin(), dirtyKeys.end());
        relabel(occupancy, m_workKeys);
        stitch();
    }

    void rebuild(const ChunkedField2D<std::uint8_t> &occupancy) {
        m_chunkIndex.clear();
        m_chunks.clear();
        occupancy.chunkKeys(m_workKeys);
        relabel(occupancy, m_workKeys);
        stitch();
        m_initialized = true;
    }

    // Region ids are epoch-scoped: every update() or rebuild() renumbers them,
    // so an id is comparable only with ids read since the same call. Within
    // one epoch the numbering is canonical (ascending packed FieldChunkKey,
    // then first row-major free cell), so it is identical on every platform.
    FieldRegionId labelAt(IRMath::ivec2 cell) const {
        const auto it = m_chunkIndex.find(packFieldChunkKey(fieldChunkOf(cell)));
        if (it == m_chunkIndex.end()) {
            return kInvalidFieldRegion;
        }
        const FieldChunkLabels &labels = m_chunks[it->second];
        const LocalLabel local = labels.local_[fieldChunkLocalIndex(fieldChunkLocal(cell))];
        if (local == kOccupiedLocal) {
            return kInvalidFieldRegion;
        }
        return m_globalIds[labels.nodeBegin_ + local - 1];
    }

    // False whenever either cell is invalid, including a cell compared with
    // itself; true for a free cell compared with itself.
    bool sameRegion(IRMath::ivec2 a, IRMath::ivec2 b) const {
        const FieldRegionId labelA = labelAt(a);
        return labelA != kInvalidFieldRegion && labelA == labelAt(b);
    }

    std::size_t chunkCount() const {
        return m_chunks.size();
    }

  private:
    using LocalLabel = std::uint16_t;
    static constexpr LocalLabel kOccupiedLocal = 0;
    // Cells that start a component (final or provisional) are never
    // 4-adjacent, so a checkerboard — half the cells — is the ceiling.
    static constexpr std::size_t kMaxLocalComponents = kFieldChunkCells / 2;

    struct FieldChunkLabels {
        FieldChunkKey key_ = 0;
        std::array<LocalLabel, kFieldChunkCells> local_{};
        std::uint32_t componentCount_ = 0;
        std::uint32_t nodeBegin_ = 0;
    };

    std::unordered_map<FieldChunkKey, std::uint32_t> m_chunkIndex;
    std::vector<FieldChunkLabels> m_chunks;
    std::vector<std::uint32_t> m_orderedChunks;
    std::vector<FieldRegionId> m_globalIds;
    detail::UnionFind m_seamUnion;
    detail::UnionFind m_localUnion;
    std::vector<LocalLabel> m_provisionalToFinal;
    std::vector<FieldChunkKey> m_workKeys;
    bool m_initialized = false;

    FieldChunkLabels &findOrCreateLabels(FieldChunkKey key) {
        auto [it, inserted] =
            m_chunkIndex.try_emplace(key, static_cast<std::uint32_t>(m_chunks.size()));
        if (inserted) {
            m_chunks.emplace_back().key_ = key;
        }
        return m_chunks[it->second];
    }

    const FieldChunkLabels *findLabels(IRMath::ivec2 chunkCoord) const {
        const auto it = m_chunkIndex.find(packFieldChunkKey(chunkCoord));
        return it == m_chunkIndex.end() ? nullptr : &m_chunks[it->second];
    }

    void
    relabel(const ChunkedField2D<std::uint8_t> &occupancy, std::span<const FieldChunkKey> keys) {
        for (FieldChunkKey key : keys) {
            const auto *fieldChunk = occupancy.findChunk(unpackFieldChunkKey(key));
            if (fieldChunk == nullptr) {
                continue;
            }
            labelFieldChunk(fieldChunk->cells(), findOrCreateLabels(key));
        }
    }

    // Classic two-pass labelling; final local ids are numbered by each
    // component's first row-major free cell, which is what the canonical
    // global numbering orders by within a field chunk.
    void labelFieldChunk(
        std::span<const std::uint8_t, kFieldChunkCells> cells, FieldChunkLabels &labels
    ) {
        m_localUnion.reset(1);
        m_localUnion.reserve(kMaxLocalComponents + 1);
        for (int y = 0; y < kFieldChunkEdge; ++y) {
            for (int x = 0; x < kFieldChunkEdge; ++x) {
                const int index = y * kFieldChunkEdge + x;
                if (cells[index] != 0) {
                    labels.local_[index] = kOccupiedLocal;
                    continue;
                }
                const LocalLabel left = x > 0 ? labels.local_[index - 1] : kOccupiedLocal;
                const LocalLabel up =
                    y > 0 ? labels.local_[index - kFieldChunkEdge] : kOccupiedLocal;
                if (left == kOccupiedLocal && up == kOccupiedLocal) {
                    labels.local_[index] = static_cast<LocalLabel>(m_localUnion.add());
                } else if (left == kOccupiedLocal) {
                    labels.local_[index] = up;
                } else {
                    labels.local_[index] = left;
                    if (up != kOccupiedLocal) {
                        m_localUnion.unite(left, up);
                    }
                }
            }
        }

        m_provisionalToFinal.assign(m_localUnion.size(), kOccupiedLocal);
        LocalLabel finalCount = 0;
        for (int index = 0; index < kFieldChunkCells; ++index) {
            const LocalLabel provisional = labels.local_[index];
            if (provisional == kOccupiedLocal) {
                continue;
            }
            const std::uint32_t root = m_localUnion.find(provisional);
            if (m_provisionalToFinal[root] == kOccupiedLocal) {
                m_provisionalToFinal[root] = ++finalCount;
            }
            labels.local_[index] = m_provisionalToFinal[root];
        }
        labels.componentCount_ = finalCount;
    }

    // Rebuilds the seam union-find and every remap from scratch: retained
    // union edges could never represent a split, and the cost is bounded by
    // the present field chunks, not by what changed.
    void stitch() {
        m_orderedChunks.resize(m_chunks.size());
        std::iota(m_orderedChunks.begin(), m_orderedChunks.end(), 0u);
        std::sort(
            m_orderedChunks.begin(),
            m_orderedChunks.end(),
            [this](std::uint32_t a, std::uint32_t b) { return m_chunks[a].key_ < m_chunks[b].key_; }
        );

        std::uint32_t nodeCount = 0;
        for (std::uint32_t chunkIndex : m_orderedChunks) {
            m_chunks[chunkIndex].nodeBegin_ = nodeCount;
            nodeCount += m_chunks[chunkIndex].componentCount_;
        }
        m_seamUnion.reset(nodeCount);

        for (std::uint32_t chunkIndex : m_orderedChunks) {
            const FieldChunkLabels &labels = m_chunks[chunkIndex];
            const IRMath::ivec2 coord = unpackFieldChunkKey(labels.key_);
            const std::int64_t rightX = static_cast<std::int64_t>(coord.x) + 1;
            const std::int64_t downY = static_cast<std::int64_t>(coord.y) + 1;
            if (std::in_range<std::int32_t>(rightX)) {
                const FieldChunkLabels *right =
                    findLabels({static_cast<std::int32_t>(rightX), coord.y});
                if (right != nullptr) {
                    for (int y = 0; y < kFieldChunkEdge; ++y) {
                        uniteSeamCells(
                            labels,
                            y * kFieldChunkEdge + kFieldChunkEdge - 1,
                            *right,
                            y * kFieldChunkEdge
                        );
                    }
                }
            }
            if (std::in_range<std::int32_t>(downY)) {
                const FieldChunkLabels *down =
                    findLabels({coord.x, static_cast<std::int32_t>(downY)});
                if (down != nullptr) {
                    for (int x = 0; x < kFieldChunkEdge; ++x) {
                        uniteSeamCells(
                            labels,
                            (kFieldChunkEdge - 1) * kFieldChunkEdge + x,
                            *down,
                            x
                        );
                    }
                }
            }
        }

        m_globalIds.assign(nodeCount, kInvalidFieldRegion);
        FieldRegionId nextId = kInvalidFieldRegion;
        for (std::uint32_t chunkIndex : m_orderedChunks) {
            const FieldChunkLabels &labels = m_chunks[chunkIndex];
            for (std::uint32_t local = 0; local < labels.componentCount_; ++local) {
                const std::uint32_t node = labels.nodeBegin_ + local;
                const std::uint32_t root = m_seamUnion.find(node);
                if (m_globalIds[root] == kInvalidFieldRegion) {
                    m_globalIds[root] = ++nextId;
                }
                m_globalIds[node] = m_globalIds[root];
            }
        }
    }

    void
    uniteSeamCells(const FieldChunkLabels &a, int indexA, const FieldChunkLabels &b, int indexB) {
        const LocalLabel localA = a.local_[indexA];
        const LocalLabel localB = b.local_[indexB];
        if (localA == kOccupiedLocal || localB == kOccupiedLocal) {
            return;
        }
        m_seamUnion.unite(a.nodeBegin_ + localA - 1, b.nodeBegin_ + localB - 1);
    }
};

} // namespace IRPrefab::Spatial

#endif /* IR_FIELD_REGIONS_H */
