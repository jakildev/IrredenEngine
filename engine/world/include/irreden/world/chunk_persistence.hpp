#ifndef IRREDEN_WORLD_CHUNK_PERSISTENCE_H
#define IRREDEN_WORLD_CHUNK_PERSISTENCE_H

// Chunk disk persistence (Epic E / E6, design at
// docs/design/world-streaming.md §"Topic 6 — File layout"). One `.vxs`
// file per chunk under `<saveRoot>/chunks/`. Filename is derived from
// the chunk's signed-integer coord:
//
//     chunks/<sx>NNNNN_<sy>NNNNN_<sz>NNNNN.vxs
//
// where `<sx>`/`<sy>`/`<sz>` is `+` or `-` and `NNNNN` is zero-padded
// |axis| (axes are int16, so 5 digits cover ±32767).
//
// Save/load route through `IRAsset::saveDenseVoxelSet` /
// `loadDenseVoxelSet` so the on-disk format is the same DENSE-mode
// `.vxs` container every other voxel-set asset uses. Writers emit
// VRLE alongside VOXR (B3 / #940) so a hollow chunk's payload sits
// around ~10 % of the worst-case 32³ × 12 B = 384 KB.
//
// E1 / this slice is synchronous — the residency manager calls save
// on dirty evict and load on first request. E2/E3 will lift the same
// calls into the residency worker pool; the surface stays the same.
//
// File layout is flat under `chunks/` in v1; the two-level directory
// split sketched in the design doc is a follow-up gated on profiling
// the per-directory file count.

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/world/chunk_coord.hpp>

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace IRWorld {

/// Stateless helper that knows where chunk files live for a given save.
/// Construct one per save root; reusable across every chunk under it.
class ChunkDiskPersistence {
  public:
    explicit ChunkDiskPersistence(std::string saveRoot);

    /// Absolute path to the `.vxs` file for @p key. Exposed for tests
    /// and for editor tooling that wants to surface "where on disk
    /// does this chunk live"; callers that just want to save/load
    /// should not need it.
    std::string chunkPath(IRPrefab::Chunk::ChunkKey key) const;

    /// Serialize @p voxels into the chunk file under @p key. The
    /// per-chunk voxel volume is `IRConstants::kChunkSize` (cubic,
    /// 32³); @p voxels must hold exactly that many records (mismatch
    /// returns `WriteFailed` with a diagnostic, leaves disk untouched).
    /// Creates any missing parent directories before writing.
    IRAsset::BinaryStatus saveChunk(
        IRPrefab::Chunk::ChunkKey key, std::span<const IRAsset::VoxelRecord> voxels
    );

    /// Load the chunk file under @p key. Returns `std::nullopt` when:
    /// - the file does not exist (chunk has never been saved — caller
    ///   treats this as "fresh empty chunk"), or
    /// - the file is malformed (bad magic, truncated, version newer
    ///   than the loader knows about, record count != chunk volume —
    ///   the underlying diagnostic is logged with file path + offset).
    /// On success returns the chunk-volume-sized record array.
    ///
    /// Save Format Extensibility Rule #5: unknown is recoverable,
    /// never fatal. The caller proceeds with an empty chunk either way.
    std::optional<std::vector<IRAsset::VoxelRecord>>
    loadChunk(IRPrefab::Chunk::ChunkKey key) const;

    /// True when the chunk file under @p key exists on disk. Cheap —
    /// a single `stat`-equivalent call; safe to use as a "should I
    /// load or seed?" gate.
    bool chunkExists(IRPrefab::Chunk::ChunkKey key) const;

    const std::string &saveRoot() const {
        return m_saveRoot;
    }

  private:
    std::string m_saveRoot;
    std::string m_chunksDir;
};

} // namespace IRWorld

#endif /* IRREDEN_WORLD_CHUNK_PERSISTENCE_H */
