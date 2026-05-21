#include <irreden/world/chunk_persistence.hpp>

#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/world/chunk_coord.hpp>

#include <filesystem>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace IRWorld {

namespace {

constexpr int kChunkVolume = static_cast<int>(IRConstants::kChunkSize.x) *
                             static_cast<int>(IRConstants::kChunkSize.y) *
                             static_cast<int>(IRConstants::kChunkSize.z);

std::string axisFragment(int v) {
    const int magnitude = (v >= 0) ? v : -v;
    std::ostringstream oss;
    oss << (v >= 0 ? '+' : '-') << std::setw(5) << std::setfill('0') << magnitude;
    return oss.str();
}

std::string filenameForKey(IRPrefab::Chunk::ChunkKey key) {
    auto coord = IRPrefab::Chunk::unpack(key);
    return axisFragment(coord.x) + "_" + axisFragment(coord.y) + "_" + axisFragment(coord.z) +
           ".vxs";
}

} // namespace

ChunkDiskPersistence::ChunkDiskPersistence(std::string saveRoot)
    : m_saveRoot{std::move(saveRoot)}
    , m_chunksDir{(std::filesystem::path{m_saveRoot} / "chunks").string()} {}

std::string ChunkDiskPersistence::chunkPath(IRPrefab::Chunk::ChunkKey key) const {
    return (std::filesystem::path{m_chunksDir} / filenameForKey(key)).string();
}

IRAsset::BinaryStatus ChunkDiskPersistence::saveChunk(
    IRPrefab::Chunk::ChunkKey key, std::span<const IRAsset::VoxelRecord> voxels
) {
    if (static_cast<int>(voxels.size()) != kChunkVolume) {
        std::ostringstream oss;
        oss << "ChunkDiskPersistence::saveChunk: voxel span size " << voxels.size()
            << " does not match chunk volume " << kChunkVolume;
        IRE_LOG_ERROR("{}", oss.str());
        return IRAsset::BinaryStatus::error(IRAsset::BinaryIOError::WriteFailed, oss.str());
    }

    std::error_code ec;
    std::filesystem::create_directories(m_chunksDir, ec);
    if (ec) {
        const std::string msg =
            "ChunkDiskPersistence::saveChunk: create_directories failed: " + ec.message() + " (" +
            m_chunksDir + ")";
        IRE_LOG_ERROR("{}", msg);
        return IRAsset::BinaryStatus::error(IRAsset::BinaryIOError::OpenFailed, msg);
    }

    auto chunkCoord = IRPrefab::Chunk::unpack(key);
    auto origin = IRPrefab::Chunk::chunkOriginVoxel(chunkCoord);

    IRAsset::DenseVoxelSet dense;
    dense.boundsMin_ = origin;
    dense.boundsMax_ = origin + IRMath::ivec3{IRConstants::kChunkSize};
    dense.voxels_.assign(voxels.begin(), voxels.end());

    return IRAsset::saveDenseVoxelSet(chunkPath(key), dense);
}

std::optional<std::vector<IRAsset::VoxelRecord>>
ChunkDiskPersistence::loadChunk(IRPrefab::Chunk::ChunkKey key) const {
    const std::string path = chunkPath(key);

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return std::nullopt;
    }

    auto result = IRAsset::loadDenseVoxelSet(path);
    if (!result.ok()) {
        IRE_LOG_WARN(
            "ChunkDiskPersistence::loadChunk: {} read failed (code={}): {}",
            path,
            static_cast<int>(result.status_.code_),
            result.status_.message_
        );
        return std::nullopt;
    }

    auto &dense = result.value_.dense_;
    if (dense.voxels_.size() != static_cast<std::size_t>(kChunkVolume)) {
        IRE_LOG_WARN(
            "ChunkDiskPersistence::loadChunk: {} record count {} != chunk volume {}; treating "
            "as empty",
            path,
            dense.voxels_.size(),
            kChunkVolume
        );
        return std::nullopt;
    }
    return std::move(dense.voxels_);
}

bool ChunkDiskPersistence::chunkExists(IRPrefab::Chunk::ChunkKey key) const {
    std::error_code ec;
    return std::filesystem::exists(chunkPath(key), ec) && !ec;
}

} // namespace IRWorld
