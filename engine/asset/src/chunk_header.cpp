#include <irreden/asset/chunk_header.hpp>

#include <cstring>
#include <utility>

namespace IRAsset {

BinaryStatus writeChunked(
    BinaryWriter &w,
    const std::array<char, 4> &magic,
    std::uint32_t version,
    std::span<const ChunkPayload> chunks
) {
    // Header
    w.writeBytes(magic.data(), 4);
    w.writeU32(version);
    w.writeU32(static_cast<std::uint32_t>(chunks.size()));

    // First chunk body starts immediately after the table.
    std::uint64_t cursor = static_cast<std::uint64_t>(kAssetHeaderSize) +
                           static_cast<std::uint64_t>(kChunkTableEntrySize) * chunks.size();

    for (const auto &c : chunks) {
        w.writeBytes(c.tag_.data(), 4);
        w.writeU64(cursor);
        w.writeU64(static_cast<std::uint64_t>(c.data_.size()));
        cursor += c.data_.size();
    }

    for (const auto &c : chunks) {
        if (!c.data_.empty()) {
            w.writeBytes(c.data_.data(), c.data_.size());
        }
    }

    if (w.failed()) {
        return BinaryStatus::error(BinaryIOError::WriteFailed, w.failureMessage());
    }
    return BinaryStatus::success();
}

Result<AssetHeader> readHeader(BinaryReader &r) {
    AssetHeader h{};
    if (auto st = r.readBytes(h.magic_.data(), 4); !st.ok()) {
        return Result<AssetHeader>::error(st.code_, std::move(st.message_));
    }
    auto verR = r.readU32();
    if (!verR.ok()) {
        return Result<AssetHeader>::error(verR.status_.code_, std::move(verR.status_.message_));
    }
    h.version_ = verR.value_;
    auto cntR = r.readU32();
    if (!cntR.ok()) {
        return Result<AssetHeader>::error(cntR.status_.code_, std::move(cntR.status_.message_));
    }
    h.chunkCount_ = cntR.value_;
    return Result<AssetHeader>::success(h);
}

Result<std::vector<ChunkTableEntry>> readChunkTable(BinaryReader &r, const AssetHeader &header) {
    std::vector<ChunkTableEntry> table;
    table.reserve(header.chunkCount_);
    const std::uint64_t totalSize = r.size();
    for (std::uint32_t i = 0; i < header.chunkCount_; ++i) {
        ChunkTableEntry e{};
        if (auto st = r.readBytes(e.tag_.data(), 4); !st.ok()) {
            return Result<std::vector<ChunkTableEntry>>::error(st.code_, std::move(st.message_));
        }
        auto offR = r.readU64();
        if (!offR.ok()) {
            return Result<std::vector<ChunkTableEntry>>::error(
                offR.status_.code_,
                std::move(offR.status_.message_)
            );
        }
        e.offset_ = offR.value_;
        auto szR = r.readU64();
        if (!szR.ok()) {
            return Result<std::vector<ChunkTableEntry>>::error(
                szR.status_.code_,
                std::move(szR.status_.message_)
            );
        }
        e.size_ = szR.value_;

        // Validate the chunk body lies fully within the source. The second guard
        // is evaluated only when e.offset_ <= totalSize, so the subtraction is safe.
        if (e.offset_ > totalSize || e.size_ > totalSize - e.offset_) {
            return Result<std::vector<ChunkTableEntry>>::error(
                BinaryIOError::ChunkOutOfBounds,
                "chunk '" + tagToString(e.tag_) + "' at offset " + std::to_string(e.offset_) +
                    " size " + std::to_string(e.size_) + " exceeds source size " +
                    std::to_string(totalSize) + " in " + r.sourceName()
            );
        }
        table.push_back(std::move(e));
    }
    return Result<std::vector<ChunkTableEntry>>::success(std::move(table));
}

Result<std::vector<LoadedChunk>> readChunks(
    BinaryReader &r,
    const std::array<char, 4> &expectedMagic,
    std::uint32_t maxKnownVersion,
    AssetHeader *outHeader
) {
    auto headerR = readHeader(r);
    if (!headerR.ok()) {
        return Result<std::vector<LoadedChunk>>::error(
            headerR.status_.code_,
            std::move(headerR.status_.message_)
        );
    }
    if (outHeader) {
        *outHeader = headerR.value_;
    }
    if (!tagsEqual(headerR.value_.magic_, expectedMagic)) {
        return Result<std::vector<LoadedChunk>>::error(
            BinaryIOError::BadMagic,
            "bad magic '" + tagToString(headerR.value_.magic_) + "' (expected '" +
                tagToString(expectedMagic) + "') in " + r.sourceName()
        );
    }
    if (headerR.value_.version_ > maxKnownVersion) {
        return Result<std::vector<LoadedChunk>>::error(
            BinaryIOError::VersionTooNew,
            "version " + std::to_string(headerR.value_.version_) + " above max known " +
                std::to_string(maxKnownVersion) + " in " + r.sourceName()
        );
    }

    auto tableR = readChunkTable(r, headerR.value_);
    if (!tableR.ok()) {
        return Result<std::vector<LoadedChunk>>::error(
            tableR.status_.code_,
            std::move(tableR.status_.message_)
        );
    }

    std::vector<LoadedChunk> chunks;
    chunks.reserve(tableR.value_.size());
    for (const auto &entry : tableR.value_) {
        if (auto st = r.seek(entry.offset_); !st.ok()) {
            return Result<std::vector<LoadedChunk>>::error(st.code_, std::move(st.message_));
        }
        LoadedChunk lc;
        lc.tag_ = entry.tag_;
        lc.data_.resize(static_cast<std::size_t>(entry.size_));
        if (entry.size_ > 0) {
            if (auto st = r.readBytes(lc.data_.data(), lc.data_.size()); !st.ok()) {
                return Result<std::vector<LoadedChunk>>::error(st.code_, std::move(st.message_));
            }
        }
        chunks.push_back(std::move(lc));
    }
    return Result<std::vector<LoadedChunk>>::success(std::move(chunks));
}

const LoadedChunk *findChunk(std::span<const LoadedChunk> chunks, const std::array<char, 4> &tag) {
    for (const auto &c : chunks) {
        if (tagsEqual(c.tag_, tag)) {
            return &c;
        }
    }
    return nullptr;
}

} // namespace IRAsset
