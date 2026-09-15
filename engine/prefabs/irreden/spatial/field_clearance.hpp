#ifndef IR_FIELD_CLEARANCE_H
#define IR_FIELD_CLEARANCE_H

#include <irreden/ir_math.hpp>
#include <irreden/spatial/chunked_field.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace IRPrefab::Spatial {

constexpr int kMaxClearanceCells = 1024;

namespace detail {

struct FieldChunkGroup {
    std::int32_t minX_ = 0;
    std::int32_t maxX_ = 0;
    std::int32_t minY_ = 0;
    std::int32_t maxY_ = 0;
};

inline void groupRectangularFieldChunks(
    std::span<const FieldChunkKey> keys,
    std::vector<FieldChunkKey> &ordered,
    std::vector<bool> &visited,
    std::vector<FieldChunkGroup> &groups
) {
    ordered.assign(keys.begin(), keys.end());
    std::sort(ordered.begin(), ordered.end());
    ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());
    visited.assign(ordered.size(), false);
    groups.clear();

    for (std::size_t rootIndex = 0; rootIndex < ordered.size(); ++rootIndex) {
        if (visited[rootIndex]) {
            continue;
        }

        const IRMath::ivec2 root = unpackFieldChunkKey(ordered[rootIndex]);
        FieldChunkGroup group{root.x, root.x, root.y, root.y};
        while (group.maxX_ < std::numeric_limits<std::int32_t>::max()) {
            const FieldChunkKey nextKey = packFieldChunkKey({group.maxX_ + 1, root.y});
            const auto nextIt = std::lower_bound(ordered.begin(), ordered.end(), nextKey);
            if (nextIt == ordered.end() || *nextIt != nextKey ||
                visited[static_cast<std::size_t>(nextIt - ordered.begin())]) {
                break;
            }
            group.maxX_ += 1;
        }

        while (group.maxY_ < std::numeric_limits<std::int32_t>::max()) {
            const std::int32_t nextY = group.maxY_ + 1;
            bool completeRow = true;
            for (std::int64_t x = group.minX_; x <= group.maxX_; ++x) {
                const FieldChunkKey nextKey =
                    packFieldChunkKey({static_cast<std::int32_t>(x), nextY});
                const auto nextIt = std::lower_bound(ordered.begin(), ordered.end(), nextKey);
                if (nextIt == ordered.end() || *nextIt != nextKey ||
                    visited[static_cast<std::size_t>(nextIt - ordered.begin())]) {
                    completeRow = false;
                    break;
                }
            }
            if (!completeRow) {
                break;
            }
            group.maxY_ = nextY;
        }

        for (std::int64_t y = group.minY_; y <= group.maxY_; ++y) {
            for (std::int64_t x = group.minX_; x <= group.maxX_; ++x) {
                const FieldChunkKey key =
                    packFieldChunkKey({static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)});
                const auto it = std::lower_bound(ordered.begin(), ordered.end(), key);
                visited[static_cast<std::size_t>(it - ordered.begin())] = true;
            }
        }
        groups.push_back(group);
    }
}

} // namespace detail

class FieldClearance {
  public:
    explicit FieldClearance(int maxClearance)
        : m_maxClearance(maxClearance) {
        if (maxClearance < 1 || maxClearance > kMaxClearanceCells) {
            throw std::invalid_argument("maximum clearance is outside the supported domain");
        }
    }

    int maxClearance() const {
        return m_maxClearance;
    }

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
        m_workKeys.assign(dirtyKeys.begin(), dirtyKeys.end());
        recompute(occupancy, m_workKeys);
        m_initialized = true;
    }

    void rebuild(const ChunkedField2D<std::uint8_t> &occupancy) {
        m_values.clear();
        occupancy.chunkKeys(m_workKeys);
        recompute(occupancy, m_workKeys);
        m_initialized = true;
    }

    const ChunkedField2D<std::int32_t> &values() const {
        return m_values;
    }

    bool hasClearance(IRMath::ivec2 cell, int clearance) const {
        if (clearance < 0 || clearance > m_maxClearance) {
            throw std::invalid_argument("requested clearance is outside the field domain");
        }

        std::int32_t clearanceSq = 0;
        if (!m_values.getCell(cell, clearanceSq) || clearanceSq <= 0) {
            return false;
        }
        const std::int64_t requestedSq = static_cast<std::int64_t>(clearance) * clearance;
        return requestedSq <= clearanceSq;
    }

  private:
    ChunkedField2D<std::int32_t> m_values;
    const int m_maxClearance;
    bool m_initialized = false;
    std::vector<FieldChunkKey> m_workKeys;
    std::vector<std::int64_t> m_grid;
    std::vector<std::int64_t> m_lineInput;
    std::vector<std::int64_t> m_lineOutput;
    std::vector<FieldChunkKey> m_orderedKeys;
    std::vector<bool> m_visitedKeys;
    std::vector<detail::FieldChunkGroup> m_groups;
    IRMath::SquaredEdtScratch m_edtScratch;

    static bool isCellCoordinate(std::int64_t value) {
        return std::in_range<std::int32_t>(value);
    }

    void
    recompute(const ChunkedField2D<std::uint8_t> &occupancy, std::span<const FieldChunkKey> keys) {
        detail::groupRectangularFieldChunks(keys, m_orderedKeys, m_visitedKeys, m_groups);
        for (const detail::FieldChunkGroup &group : m_groups) {
            recomputeGroup(occupancy, group);
        }
        m_values.update();
    }

    void recomputeGroup(
        const ChunkedField2D<std::uint8_t> &occupancy, const detail::FieldChunkGroup &group
    ) {
        const std::int64_t dirtyMinX = static_cast<std::int64_t>(group.minX_) * kFieldChunkEdge;
        const std::int64_t dirtyMaxX =
            (static_cast<std::int64_t>(group.maxX_) + 1) * kFieldChunkEdge - 1;
        const std::int64_t dirtyMinY = static_cast<std::int64_t>(group.minY_) * kFieldChunkEdge;
        const std::int64_t dirtyMaxY =
            (static_cast<std::int64_t>(group.maxY_) + 1) * kFieldChunkEdge - 1;
        const std::int64_t computeHalo = static_cast<std::int64_t>(m_maxClearance) * 2;
        const std::int64_t computeMinX = dirtyMinX - computeHalo;
        const std::int64_t computeMaxX = dirtyMaxX + computeHalo;
        const std::int64_t computeMinY = dirtyMinY - computeHalo;
        const std::int64_t computeMaxY = dirtyMaxY + computeHalo;
        const std::int64_t writeMinX = dirtyMinX - m_maxClearance;
        const std::int64_t writeMaxX = dirtyMaxX + m_maxClearance;
        const std::int64_t writeMinY = dirtyMinY - m_maxClearance;
        const std::int64_t writeMaxY = dirtyMaxY + m_maxClearance;
        const std::size_t width = checkedExtent(computeMinX, computeMaxX);
        const std::size_t height = checkedExtent(computeMinY, computeMaxY);
        if (height > m_grid.max_size() / width) {
            throw std::length_error("clearance window is too large");
        }
        m_grid.resize(width * height);
        m_lineInput.resize(width);
        m_lineOutput.resize(width);

        for (std::size_t y = 0; y < height; ++y) {
            const std::int64_t cellY = computeMinY + static_cast<std::int64_t>(y);
            if (!isCellCoordinate(cellY)) {
                std::fill(m_lineInput.begin(), m_lineInput.end(), 0);
                IRMath::squaredDistanceTransform1D(m_lineInput, m_lineOutput, m_edtScratch);
                std::copy(m_lineOutput.begin(), m_lineOutput.end(), m_grid.begin() + y * width);
                continue;
            }

            const auto cellY32 = static_cast<std::int32_t>(cellY);
            const std::int32_t chunkY = cellY32 >> kFieldChunkShift;
            const int localY = cellY32 & kFieldChunkLocalMask;
            std::int32_t cachedChunkX = 0;
            bool hasCachedChunk = false;
            const ChunkedField2D<std::uint8_t>::FieldChunk *fieldChunk = nullptr;
            for (std::size_t x = 0; x < width; ++x) {
                const std::int64_t cellX = computeMinX + static_cast<std::int64_t>(x);
                if (!isCellCoordinate(cellX)) {
                    m_lineInput[x] = 0;
                    continue;
                }
                const auto cellX32 = static_cast<std::int32_t>(cellX);
                const std::int32_t chunkX = cellX32 >> kFieldChunkShift;
                if (!hasCachedChunk || cachedChunkX != chunkX) {
                    cachedChunkX = chunkX;
                    hasCachedChunk = true;
                    fieldChunk = occupancy.findChunk({chunkX, chunkY});
                }
                const bool free =
                    fieldChunk != nullptr && fieldChunk->cells()[fieldChunkLocalIndex(
                                                 {cellX32 & kFieldChunkLocalMask, localY}
                                             )] == 0;
                m_lineInput[x] = free ? IRMath::kSquaredEdtInfinity : 0;
            }
            IRMath::squaredDistanceTransform1D(m_lineInput, m_lineOutput, m_edtScratch);
            std::copy(m_lineOutput.begin(), m_lineOutput.end(), m_grid.begin() + y * width);
        }

        m_lineInput.resize(height);
        m_lineOutput.resize(height);
        const std::int64_t capSq = static_cast<std::int64_t>(m_maxClearance) * m_maxClearance;
        for (std::size_t x = 0; x < width; ++x) {
            const std::int64_t cellX = computeMinX + static_cast<std::int64_t>(x);
            if (cellX < writeMinX || cellX > writeMaxX || !isCellCoordinate(cellX)) {
                continue;
            }
            for (std::size_t y = 0; y < height; ++y) {
                m_lineInput[y] = m_grid[y * width + x];
            }
            IRMath::squaredDistanceTransform1D(m_lineInput, m_lineOutput, m_edtScratch);

            std::int32_t cachedChunkY = 0;
            bool hasCachedChunk = false;
            const ChunkedField2D<std::uint8_t>::FieldChunk *fieldChunk = nullptr;
            for (std::size_t y = 0; y < height; ++y) {
                const std::int64_t cellY = computeMinY + static_cast<std::int64_t>(y);
                if (cellY < writeMinY || cellY > writeMaxY || !isCellCoordinate(cellY)) {
                    continue;
                }
                const IRMath::ivec2 cell{
                    static_cast<std::int32_t>(cellX),
                    static_cast<std::int32_t>(cellY),
                };
                const std::int32_t chunkY = cell.y >> kFieldChunkShift;
                if (!hasCachedChunk || cachedChunkY != chunkY) {
                    cachedChunkY = chunkY;
                    hasCachedChunk = true;
                    fieldChunk = occupancy.findChunk({cell.x >> kFieldChunkShift, chunkY});
                }
                if (fieldChunk == nullptr) {
                    continue;
                }
                const std::int64_t value = IRMath::min(m_lineOutput[y], capSq);
                m_values.setCell(cell, static_cast<std::int32_t>(value));
            }
        }
    }

    static std::size_t checkedExtent(std::int64_t minimum, std::int64_t maximum) {
        const std::uint64_t extent = static_cast<std::uint64_t>(maximum - minimum) + 1;
        IRMath::detail::validateSquaredEdtSize(static_cast<std::size_t>(extent));
        return static_cast<std::size_t>(extent);
    }
};

} // namespace IRPrefab::Spatial

#endif /* IR_FIELD_CLEARANCE_H */
