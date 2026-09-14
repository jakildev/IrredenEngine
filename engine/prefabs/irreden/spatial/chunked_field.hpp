#ifndef IR_CHUNKED_FIELD_H
#define IR_CHUNKED_FIELD_H

// PURPOSE: Sparse storage for integer-valued 2D cell fields. Dense field chunk
//   buffers are retained across clear() while logical presence remains map
//   membership. See docs/design/chunked-field-placement-kit.md.

#include <irreden/ir_math.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace IRPrefab::Spatial {

constexpr int kFieldChunkEdge = 32;
constexpr int kFieldChunkShift = 5;
constexpr int kFieldChunkLocalMask = 31;
constexpr int kFieldChunkCells = kFieldChunkEdge * kFieldChunkEdge;

using FieldChunkKey = std::uint64_t;

constexpr FieldChunkKey packFieldChunkKey(IRMath::ivec2 chunkCoord) {
    const auto x = static_cast<std::uint32_t>(chunkCoord.x);
    const auto y = static_cast<std::uint32_t>(chunkCoord.y);
    return static_cast<FieldChunkKey>(x) | (static_cast<FieldChunkKey>(y) << 32);
}

constexpr IRMath::ivec2 unpackFieldChunkKey(FieldChunkKey key) {
    const auto x = static_cast<std::int32_t>(static_cast<std::uint32_t>(key));
    const auto y = static_cast<std::int32_t>(static_cast<std::uint32_t>(key >> 32));
    return {x, y};
}

constexpr IRMath::ivec2 fieldChunkOf(IRMath::ivec2 cell) {
    return cell >> kFieldChunkShift;
}

constexpr IRMath::ivec2 fieldChunkLocal(IRMath::ivec2 cell) {
    return cell & kFieldChunkLocalMask;
}

constexpr int fieldChunkLocalIndex(IRMath::ivec2 local) {
    return IRMath::index2DtoIndex1D(local, {kFieldChunkEdge, kFieldChunkEdge});
}

template <typename T> class ChunkedField2D {
    static_assert(std::is_integral_v<T>, "ChunkedField2D values must be integers");

  public:
    struct FieldChunk {
        // The returned view, like a FieldChunk pointer, is invalid after the
        // field is cleared or destroyed and must not outlive buffer reuse.
        std::span<const T, kFieldChunkCells> cells() const {
            return std::span<const T, kFieldChunkCells>{m_cells.get(), kFieldChunkCells};
        }

        T min_{};
        T max_{};
        int nonZeroCount_ = 0;
        bool dirty_ = false;

      private:
        std::unique_ptr<T[]> m_cells;

        friend class ChunkedField2D<T>;
    };

    void setCell(IRMath::ivec2 cell, T value) {
        const FieldChunkKey key = packFieldChunkKey(fieldChunkOf(cell));
        auto [it, inserted] = m_fieldChunks.try_emplace(key);
        FieldChunk &fieldChunk = it->second;
        if (inserted) {
            fieldChunk.m_cells = acquireBuffer();
        }

        const int index = fieldChunkLocalIndex(fieldChunkLocal(cell));
        const T oldValue = fieldChunk.m_cells[index];
        if (!inserted && oldValue == value) {
            return;
        }

        if (oldValue != T{}) {
            --fieldChunk.nonZeroCount_;
        }
        if (value != T{}) {
            ++fieldChunk.nonZeroCount_;
        }
        fieldChunk.m_cells[index] = value;
        markDirty(key, fieldChunk);
    }

    void clear() {
        for (auto &[key, fieldChunk] : m_fieldChunks) {
            m_dirtyKeys.push_back(key);
            m_freeBuffers.push_back(std::move(fieldChunk.m_cells));
        }
        m_fieldChunks.clear();
    }

    void update() {
        sortAndUnique(m_dirtyKeys);
        for (FieldChunkKey key : m_dirtyKeys) {
            auto it = m_fieldChunks.find(key);
            if (it == m_fieldChunks.end()) {
                continue;
            }

            FieldChunk &fieldChunk = it->second;
            T minValue = fieldChunk.m_cells[0];
            T maxValue = minValue;
            for (int i = 1; i < kFieldChunkCells; ++i) {
                minValue = IRMath::min(minValue, fieldChunk.m_cells[i]);
                maxValue = IRMath::max(maxValue, fieldChunk.m_cells[i]);
            }
            fieldChunk.min_ = minValue;
            fieldChunk.max_ = maxValue;
            fieldChunk.dirty_ = false;
        }
        m_dirtyKeys.clear();
    }

    // Returned pointers are invalid after clear() or destruction.
    const FieldChunk *findChunk(IRMath::ivec2 chunkCoord) const {
        auto it = m_fieldChunks.find(packFieldChunkKey(chunkCoord));
        return it == m_fieldChunks.end() ? nullptr : &it->second;
    }

    bool getCell(IRMath::ivec2 cell, T &out) const {
        const FieldChunk *fieldChunk = findChunk(fieldChunkOf(cell));
        if (fieldChunk == nullptr) {
            return false;
        }
        out = fieldChunk->m_cells[fieldChunkLocalIndex(fieldChunkLocal(cell))];
        return true;
    }

    std::size_t chunkCount() const {
        return m_fieldChunks.size();
    }

    void chunkKeys(std::vector<FieldChunkKey> &out) const {
        out.clear();
        out.reserve(m_fieldChunks.size());
        for (const auto &entry : m_fieldChunks) {
            out.push_back(entry.first);
        }
        std::sort(out.begin(), out.end());
    }

    void dirtyKeys(std::vector<FieldChunkKey> &out) const {
        out.clear();
        out = m_dirtyKeys;
        sortAndUnique(out);
    }

  private:
    std::unordered_map<FieldChunkKey, FieldChunk> m_fieldChunks;
    std::vector<FieldChunkKey> m_dirtyKeys;
    std::vector<std::unique_ptr<T[]>> m_freeBuffers;

    std::unique_ptr<T[]> acquireBuffer() {
        if (m_freeBuffers.empty()) {
            return std::make_unique<T[]>(kFieldChunkCells);
        }

        std::unique_ptr<T[]> buffer = std::move(m_freeBuffers.back());
        m_freeBuffers.pop_back();
        std::fill_n(buffer.get(), kFieldChunkCells, T{});
        return buffer;
    }

    void markDirty(FieldChunkKey key, FieldChunk &fieldChunk) {
        if (fieldChunk.dirty_) {
            return;
        }
        fieldChunk.dirty_ = true;
        m_dirtyKeys.push_back(key);
    }

    static void sortAndUnique(std::vector<FieldChunkKey> &keys) {
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    }
};

} // namespace IRPrefab::Spatial

#endif /* IR_CHUNKED_FIELD_H */
