#include <irreden/world/field_chunk_persistence.hpp>

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/chunk_header.hpp>
#include <irreden/ir_profile.hpp>

#include <bit>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace IRWorld {

namespace {

constexpr std::uint32_t kFieldRegionVersion = 1;
constexpr int kBucketEdgeRegions = 64;
constexpr std::size_t kHeaderBytes = 4 + 4 + 1 + 1 + 1;
constexpr int kMaxLayerLength = 32;
constexpr const char *kRegionExtension = ".irfield";
constexpr std::size_t kCoordDigits = 10;

constexpr std::array<char, 4> kMagicTag{'I', 'R', 'F', 'D'};
constexpr std::array<char, 4> kHeaderTag{'F', 'H', 'D', 'R'};
constexpr std::array<char, 4> kMaskTag{'C', 'M', 'S', 'K'};
constexpr std::array<char, 4> kCellTag{'C', 'E', 'L', 'L'};

bool isLayerName(const std::string &layer) {
    if (layer.empty() || layer.size() > static_cast<std::size_t>(kMaxLayerLength)) {
        return false;
    }
    if (layer[0] < 'a' || layer[0] > 'z') {
        return false;
    }
    for (char c : layer) {
        const bool lower = c >= 'a' && c <= 'z';
        const bool digit = c >= '0' && c <= '9';
        if (!lower && !digit && c != '_') {
            return false;
        }
    }
    return true;
}

bool isDigits(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    for (char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

bool isBucketName(const std::string &name) {
    std::string_view text = name;
    if (!text.empty() && text[0] == '-') {
        text.remove_prefix(1);
    }
    return isDigits(text);
}

bool isSignedCoordFragment(std::string_view text) {
    return text.size() == kCoordDigits + 1 && (text[0] == '+' || text[0] == '-') &&
           isDigits(text.substr(1));
}

bool isRegionFileName(const std::string &name) {
    const std::string_view extension = kRegionExtension;
    const std::size_t fragment = kCoordDigits + 1;
    if (name.size() != fragment * 2 + 1 + extension.size()) {
        return false;
    }
    const std::string_view text = name;
    return isSignedCoordFragment(text.substr(0, fragment)) && text[fragment] == '_' &&
           isSignedCoordFragment(text.substr(fragment + 1, fragment)) &&
           text.substr(fragment * 2 + 1) == extension;
}

std::string coordFragment(int value) {
    const std::int64_t wide = value;
    std::ostringstream oss;
    oss << (wide >= 0 ? '+' : '-') << std::setw(static_cast<int>(kCoordDigits)) << std::setfill('0')
        << (wide >= 0 ? wide : -wide);
    return oss.str();
}

std::optional<std::vector<std::uint8_t>> readWholeFile(const std::string &path, bool &missing) {
    missing = false;
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        missing = errno == ENOENT;
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes;
    std::array<std::uint8_t, 65536> block;
    std::size_t read = 0;
    while ((read = std::fread(block.data(), 1, block.size(), file)) > 0) {
        bytes.insert(bytes.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(read));
    }
    const bool failed = std::ferror(file) != 0;
    std::fclose(file);
    if (failed) {
        return std::nullopt;
    }
    return bytes;
}

// Removes @p directory when it is now empty; `remove` never deletes a
// non-empty directory.
void removeIfEmptied(const std::filesystem::path &directory) {
    std::error_code ec;
    if (std::filesystem::is_empty(directory, ec) && !ec) {
        std::filesystem::remove(directory, ec);
    }
}

} // namespace

int FieldRegion::chunkCount() const {
    int count = 0;
    for (std::uint8_t byte : mask_) {
        count += std::popcount(byte);
    }
    return count;
}

FieldChunkDiskPersistence::FieldChunkDiskPersistence(
    std::string saveRoot, std::string layer, int bytesPerCell
)
    : m_saveRoot{std::move(saveRoot)}
    , m_layer{std::move(layer)}
    , m_layerDir{(std::filesystem::path{m_saveRoot} / "fields" / m_layer).string()}
    , m_bytesPerCell{bytesPerCell} {}

std::optional<FieldChunkDiskPersistence>
FieldChunkDiskPersistence::create(std::string saveRoot, std::string layer, int bytesPerCell) {
    if (saveRoot.empty() || !isLayerName(layer) || bytesPerCell < 1 || bytesPerCell > 255) {
        return std::nullopt;
    }
    return FieldChunkDiskPersistence{std::move(saveRoot), std::move(layer), bytesPerCell};
}

IRMath::ivec2 FieldChunkDiskPersistence::regionOf(IRMath::ivec2 chunkCoord) {
    return {
        static_cast<int>(IRMath::floorDiv(chunkCoord.x, kFieldRegionEdgeChunks)),
        static_cast<int>(IRMath::floorDiv(chunkCoord.y, kFieldRegionEdgeChunks))
    };
}

IRMath::ivec2 FieldChunkDiskPersistence::regionFirstChunk(IRMath::ivec2 region) {
    return region * kFieldRegionEdgeChunks;
}

int FieldChunkDiskPersistence::regionLocalBit(IRMath::ivec2 chunkCoord) {
    const IRMath::ivec2 local = chunkCoord - regionFirstChunk(regionOf(chunkCoord));
    return local.y * kFieldRegionEdgeChunks + local.x;
}

std::string FieldChunkDiskPersistence::regionPath(IRMath::ivec2 region) const {
    return (std::filesystem::path{m_layerDir} /
            std::to_string(IRMath::floorDiv(region.x, kBucketEdgeRegions)) /
            std::to_string(IRMath::floorDiv(region.y, kBucketEdgeRegions)) /
            (coordFragment(region.x) + "_" + coordFragment(region.y) + kRegionExtension))
        .string();
}

std::optional<FieldRegion> FieldChunkDiskPersistence::loadRegion(IRMath::ivec2 region) const {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_UPDATE);
    const std::string path = regionPath(region);
    bool missing = false;
    std::optional<std::vector<std::uint8_t>> bytes = readWholeFile(path, missing);
    if (!bytes.has_value()) {
        if (!missing) {
            IRE_LOG_WARN("FieldChunkDiskPersistence::loadRegion: cannot read {}", path);
        }
        return std::nullopt;
    }

    const auto malformed = [&path](const std::string &reason) {
        IRE_LOG_WARN("FieldChunkDiskPersistence::loadRegion: {} is malformed: {}", path, reason);
        return std::optional<FieldRegion>{};
    };

    IRAsset::MemoryBinaryReader reader{bytes->data(), bytes->size(), path};
    auto chunks = IRAsset::readChunks(reader, kMagicTag, kFieldRegionVersion);
    if (!chunks.ok()) {
        return malformed(chunks.status_.message_);
    }
    const IRAsset::LoadedChunk *header = IRAsset::findChunk(chunks.value_, kHeaderTag);
    const IRAsset::LoadedChunk *mask = IRAsset::findChunk(chunks.value_, kMaskTag);
    const IRAsset::LoadedChunk *cells = IRAsset::findChunk(chunks.value_, kCellTag);
    if (header == nullptr || mask == nullptr || cells == nullptr) {
        return malformed("missing FHDR, CMSK or CELL");
    }
    if (header->data_.size() < kHeaderBytes) {
        return malformed("truncated FHDR");
    }

    IRAsset::MemoryBinaryReader headerReader{header->data_.data(), header->data_.size(), path};
    const auto rx = headerReader.readI32();
    const auto ry = headerReader.readI32();
    const auto chunkEdge = headerReader.readU8();
    const auto regionEdge = headerReader.readU8();
    const auto bytesPerCell = headerReader.readU8();
    if (rx.value_ != region.x || ry.value_ != region.y) {
        return malformed("FHDR names another region");
    }
    if (chunkEdge.value_ != IRPrefab::Spatial::kFieldChunkEdge ||
        regionEdge.value_ != kFieldRegionEdgeChunks || bytesPerCell.value_ != m_bytesPerCell) {
        return malformed("FHDR schema differs from the layer's");
    }
    if (mask->data_.size() != static_cast<std::size_t>(kFieldRegionMaskBytes)) {
        return malformed("CMSK is not 32 bytes");
    }

    FieldRegion result;
    std::copy(mask->data_.begin(), mask->data_.end(), result.mask_.begin());
    if (cells->data_.size() != regionCellBytes(result.chunkCount())) {
        return malformed("CELL size disagrees with CMSK");
    }
    result.cells_ = cells->data_;
    return result;
}

bool FieldChunkDiskPersistence::saveRegion(IRMath::ivec2 region, const FieldRegion &data) const {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_UPDATE);
    const std::filesystem::path target{regionPath(region)};
    const int chunkCount = data.chunkCount();
    std::error_code ec;
    if (chunkCount == 0) {
        std::filesystem::remove(target, ec);
        return !ec;
    }
    if (data.cells_.size() != regionCellBytes(chunkCount)) {
        IRE_LOG_ERROR(
            "FieldChunkDiskPersistence::saveRegion: {} cell bytes for {} field chunks",
            data.cells_.size(),
            chunkCount
        );
        return false;
    }

    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) {
        IRE_LOG_ERROR(
            "FieldChunkDiskPersistence::saveRegion: create_directories({}) failed: {}",
            target.parent_path().string(),
            ec.message()
        );
        return false;
    }

    IRAsset::MemoryBinaryWriter headerWriter;
    headerWriter.writeI32(region.x);
    headerWriter.writeI32(region.y);
    headerWriter.writeU8(static_cast<std::uint8_t>(IRPrefab::Spatial::kFieldChunkEdge));
    headerWriter.writeU8(static_cast<std::uint8_t>(kFieldRegionEdgeChunks));
    headerWriter.writeU8(static_cast<std::uint8_t>(m_bytesPerCell));
    const std::array<IRAsset::ChunkPayload, 3> payloads{
        IRAsset::ChunkPayload{kHeaderTag, headerWriter.takeBuffer()},
        IRAsset::ChunkPayload{kMaskTag, {data.mask_.begin(), data.mask_.end()}},
        IRAsset::ChunkPayload{kCellTag, data.cells_},
    };
    IRAsset::MemoryBinaryWriter fileWriter;
    IRAsset::BinaryStatus status =
        IRAsset::writeChunked(fileWriter, kMagicTag, kFieldRegionVersion, payloads);
    if (!status.ok()) {
        IRE_LOG_ERROR("FieldChunkDiskPersistence::saveRegion: encode failed: {}", status.message_);
        return false;
    }

    std::filesystem::path temporary = target;
    temporary += ".tmp";
    std::FILE *file = std::fopen(temporary.string().c_str(), "wb");
    if (file == nullptr) {
        IRE_LOG_ERROR("FieldChunkDiskPersistence::saveRegion: cannot open {}", temporary.string());
        return false;
    }
    const std::vector<std::uint8_t> &bytes = fileWriter.buffer();
    const bool written = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed) {
        IRE_LOG_ERROR(
            "FieldChunkDiskPersistence::saveRegion: write to {} failed",
            temporary.string()
        );
        std::filesystem::remove(temporary, ec);
        return false;
    }
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        IRE_LOG_ERROR(
            "FieldChunkDiskPersistence::saveRegion: rename to {} failed: {}",
            target.string(),
            ec.message()
        );
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

bool FieldChunkDiskPersistence::regionExists(IRMath::ivec2 region) const {
    std::error_code ec;
    return std::filesystem::exists(regionPath(region), ec) && !ec;
}

int FieldChunkDiskPersistence::removeAll() const {
    std::error_code ec;
    const std::filesystem::path layerDir{m_layerDir};
    const std::filesystem::file_status layerStatus = std::filesystem::symlink_status(layerDir, ec);
    if (ec || !std::filesystem::exists(layerStatus)) {
        return 0;
    }
    if (std::filesystem::is_symlink(layerStatus) || !std::filesystem::is_directory(layerStatus)) {
        IRE_LOG_WARN(
            "FieldChunkDiskPersistence::removeAll: {} is not a plain directory; refusing",
            m_layerDir
        );
        return 0;
    }

    const auto plainSubdirectories = [](const std::filesystem::path &parent) {
        std::vector<std::filesystem::path> directories;
        std::error_code iterateError;
        for (const std::filesystem::directory_entry &entry :
             std::filesystem::directory_iterator(parent, iterateError)) {
            std::error_code statusError;
            const std::filesystem::file_status status = entry.symlink_status(statusError);
            if (!statusError && std::filesystem::is_directory(status) &&
                !std::filesystem::is_symlink(status) &&
                isBucketName(entry.path().filename().string())) {
                directories.push_back(entry.path());
            }
        }
        return directories;
    };

    int removed = 0;
    for (const std::filesystem::path &xBucket : plainSubdirectories(layerDir)) {
        bool emptiedAny = false;
        for (const std::filesystem::path &yBucket : plainSubdirectories(xBucket)) {
            std::vector<std::filesystem::path> files;
            std::error_code iterateError;
            for (const std::filesystem::directory_entry &entry :
                 std::filesystem::directory_iterator(yBucket, iterateError)) {
                std::error_code statusError;
                const std::filesystem::file_status status = entry.symlink_status(statusError);
                if (!statusError && std::filesystem::is_regular_file(status) &&
                    isRegionFileName(entry.path().filename().string())) {
                    files.push_back(entry.path());
                }
            }
            int removedHere = 0;
            for (const std::filesystem::path &file : files) {
                std::error_code removeError;
                if (std::filesystem::remove(file, removeError) && !removeError) {
                    ++removedHere;
                }
            }
            if (removedHere > 0) {
                removeIfEmptied(yBucket);
                emptiedAny = emptiedAny || !std::filesystem::exists(yBucket, ec);
            }
            removed += removedHere;
        }
        if (emptiedAny) {
            removeIfEmptied(xBucket);
        }
    }
    return removed;
}

} // namespace IRWorld
