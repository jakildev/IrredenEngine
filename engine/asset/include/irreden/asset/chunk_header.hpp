#ifndef IR_ASSET_CHUNK_HEADER_H
#define IR_ASSET_CHUNK_HEADER_H

/// Asset-file header + chunk-table primitives. Every binary asset format
/// (`.vxs`, `.rig`, the ECS world snapshot, future formats) starts with
///
///     AssetHeader  { char magic[4]; uint32 version; uint32 chunkCount; }   // 12 bytes
///     ChunkTableEntry[chunkCount]  { char tag[4]; uint64 offset; uint64 size; }
///     <chunk bodies, in arbitrary order>
///
/// Loaders iterate the chunk table and **silently skip unknown chunk
/// tags** (Save Format Extensibility Rule #1) — adding a new chunk to a
/// future writer doesn't break older readers and doesn't require a
/// version bump.
///
/// Order in the chunk table is not load-bearing: a reader's "find this
/// tag" lookup is the only path. `findChunk()` returns the first match.

#include <irreden/asset/binary_io.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace IRAsset {

/// 12-byte fixed-size asset-file header. The trailing `kHeaderSize`
/// constant is the on-disk size; do not use `sizeof(AssetHeader)` since
/// struct padding could differ across compilers.
struct AssetHeader {
    std::array<char, 4> magic_{};
    std::uint32_t version_ = 0;
    std::uint32_t chunkCount_ = 0;
};

constexpr std::size_t kAssetHeaderSize = 4 + 4 + 4;
constexpr std::size_t kChunkTableEntrySize = 4 + 8 + 8;

/// One entry in the chunk table that follows the header.
/// `offset_` is the absolute byte offset from the start of the file/buffer
/// to the chunk body. `size_` is the body length in bytes.
struct ChunkTableEntry {
    std::array<char, 4> tag_{};
    std::uint64_t offset_ = 0;
    std::uint64_t size_ = 0;
};

/// A chunk body produced by `writeChunked()`. `data_` holds the raw
/// bytes that will be written under this `tag_`.
struct ChunkPayload {
    std::array<char, 4> tag_{};
    std::vector<std::uint8_t> data_;
};

/// A chunk surfaced by `readChunks()`. `data_` holds a copy of the raw
/// bytes (the reader is fully detached from the source after `readChunks`
/// returns). Unknown chunks come through with their raw bytes intact so
/// loaders can ignore or pass-through-on-save (Extensibility Rule #1).
struct LoadedChunk {
    std::array<char, 4> tag_{};
    std::vector<std::uint8_t> data_;
};

// Silent zero-pad or truncation creates tag collisions — asserts on wrong-length input.
inline std::array<char, 4> makeTag(std::string_view s) {
    assert(s.size() == 4 && "makeTag: input must be exactly 4 characters");
    std::array<char, 4> out{};
    for (std::size_t i = 0; i < 4; ++i) {
        out[i] = s[i];
    }
    return out;
}

inline bool tagsEqual(const std::array<char, 4> &a, const std::array<char, 4> &b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

inline std::string tagToString(const std::array<char, 4> &t) {
    return std::string(t.data(), 4);
}

/// Write the 12-byte header followed by the chunk table, then each
/// chunk's body. Offsets in the table are computed automatically.
/// Returns the writer's failure state on completion.
BinaryStatus writeChunked(
    BinaryWriter &w,
    const std::array<char, 4> &magic,
    std::uint32_t version,
    std::span<const ChunkPayload> chunks
);

/// Read just the 12-byte header. Does NOT validate `magic_` — callers
/// pass an expected magic to `readChunks` for that check.
Result<AssetHeader> readHeader(BinaryReader &r);

/// Read the chunk table following @p header. Validates each entry's
/// `offset_` + `size_` fits within the reader's size; returns
/// `ChunkOutOfBounds` otherwise.
Result<std::vector<ChunkTableEntry>> readChunkTable(BinaryReader &r, const AssetHeader &header);

/// Combined helper: read header, validate magic, validate version against
/// @p maxKnownVersion, read chunk table, and pull every chunk body into
/// memory. Returns every chunk in load order (table order, which may not
/// match write order if a future writer re-orders).
///
/// `expectedMagic` mismatch → `BadMagic`. A header version above
/// @p maxKnownVersion → `VersionTooNew` (callers can still recover by
/// inspecting `r` position).
Result<std::vector<LoadedChunk>> readChunks(
    BinaryReader &r,
    const std::array<char, 4> &expectedMagic,
    std::uint32_t maxKnownVersion,
    AssetHeader *outHeader = nullptr
);

/// Linear search for the first chunk with @p tag. Returns nullptr if
/// missing. O(n) but n is tiny — every format has well under 100 chunks.
const LoadedChunk *findChunk(std::span<const LoadedChunk> chunks, const std::array<char, 4> &tag);

} // namespace IRAsset

#endif /* IR_ASSET_CHUNK_HEADER_H */
