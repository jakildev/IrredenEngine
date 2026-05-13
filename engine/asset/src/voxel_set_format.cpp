#include <irreden/asset/voxel_set_format.hpp>

#include <irreden/ir_profile.hpp>

#include <utility>

namespace IRAsset {

namespace {

void writeColorPacked(BinaryWriter &w, Color color) {
    w.writeU32(color.toPackedRGBA());
}

Color unpackColor(std::uint32_t packed) {
    return Color{
        static_cast<std::uint8_t>(packed & 0xFFu),
        static_cast<std::uint8_t>((packed >> 8) & 0xFFu),
        static_cast<std::uint8_t>((packed >> 16) & 0xFFu),
        static_cast<std::uint8_t>((packed >> 24) & 0xFFu),
    };
}

void writeVec3(BinaryWriter &w, const vec3 &v) {
    w.writeF32(v.x);
    w.writeF32(v.y);
    w.writeF32(v.z);
}

void writeVec4(BinaryWriter &w, const vec4 &v) {
    w.writeF32(v.x);
    w.writeF32(v.y);
    w.writeF32(v.z);
    w.writeF32(v.w);
}

Result<vec3> readVec3(BinaryReader &r) {
    auto x = r.readF32();
    if (!x.ok())
        return Result<vec3>::error(x.status_.code_, std::move(x.status_.message_));
    auto y = r.readF32();
    if (!y.ok())
        return Result<vec3>::error(y.status_.code_, std::move(y.status_.message_));
    auto z = r.readF32();
    if (!z.ok())
        return Result<vec3>::error(z.status_.code_, std::move(z.status_.message_));
    return Result<vec3>::success(vec3(x.value_, y.value_, z.value_));
}

Result<vec4> readVec4(BinaryReader &r) {
    auto x = r.readF32();
    if (!x.ok())
        return Result<vec4>::error(x.status_.code_, std::move(x.status_.message_));
    auto y = r.readF32();
    if (!y.ok())
        return Result<vec4>::error(y.status_.code_, std::move(y.status_.message_));
    auto z = r.readF32();
    if (!z.ok())
        return Result<vec4>::error(z.status_.code_, std::move(z.status_.message_));
    auto w = r.readF32();
    if (!w.ok())
        return Result<vec4>::error(w.status_.code_, std::move(w.status_.message_));
    return Result<vec4>::success(vec4(x.value_, y.value_, z.value_, w.value_));
}

// Canonical (id, name) table for `IRMath::SDF::ShapeType`. Keep in
// lockstep with the enum declaration. Adding a value here is the
// "extends an enum across save format" step — older builds receiving
// the new save look up by name, fail, and skip that record.
struct ShapeTypeNameEntry {
    std::uint32_t id_;
    const char *name_;
};
constexpr ShapeTypeNameEntry kShapeTypeTable[] = {
    {static_cast<std::uint32_t>(IRMath::SDF::ShapeType::BOX), "BOX"},
    {static_cast<std::uint32_t>(IRMath::SDF::ShapeType::SPHERE), "SPHERE"},
    {static_cast<std::uint32_t>(IRMath::SDF::ShapeType::CYLINDER), "CYLINDER"},
    {static_cast<std::uint32_t>(IRMath::SDF::ShapeType::ELLIPSOID), "ELLIPSOID"},
    {static_cast<std::uint32_t>(IRMath::SDF::ShapeType::CURVED_PANEL), "CURVED_PANEL"},
    {static_cast<std::uint32_t>(IRMath::SDF::ShapeType::WEDGE), "WEDGE"},
    {static_cast<std::uint32_t>(IRMath::SDF::ShapeType::TAPERED_BOX), "TAPERED_BOX"},
    {static_cast<std::uint32_t>(IRMath::SDF::ShapeType::CUSTOM_SDF), "CUSTOM_SDF"},
    {static_cast<std::uint32_t>(IRMath::SDF::ShapeType::CONE), "CONE"},
    {static_cast<std::uint32_t>(IRMath::SDF::ShapeType::TORUS), "TORUS"},
};

} // namespace

// ---- MODE chunk --------------------------------------------------------

ChunkPayload makeModeChunk(VoxelSetMode mode) {
    std::array<char, 4> tag{};
    switch (mode) {
    case VoxelSetMode::DENSE:
        tag = kModeTagDense;
        break;
    case VoxelSetMode::SHAPES:
        tag = kModeTagShapes;
        break;
    case VoxelSetMode::HYBRID:
        tag = kModeTagHybrid;
        break;
    default:
        IRE_LOG_ERROR("makeModeChunk: refusing to persist VoxelSetMode::UNKNOWN");
        return ChunkPayload{kChunkTagMode, {}};
    }
    MemoryBinaryWriter w;
    w.writeBytes(tag.data(), tag.size());
    ChunkPayload out;
    out.tag_ = kChunkTagMode;
    out.data_ = w.takeBuffer();
    return out;
}

VoxelSetMode readModeChunk(std::span<const LoadedChunk> chunks) {
    const LoadedChunk *mode = findChunk(chunks, kChunkTagMode);
    if (mode == nullptr) {
        return VoxelSetMode::SHAPES;
    }
    if (mode->data_.size() < 4) {
        IRE_LOG_ERROR("readModeChunk: MODE body is {} bytes, expected 4", mode->data_.size());
        return VoxelSetMode::UNKNOWN;
    }
    std::array<char, 4> tag{};
    MemoryBinaryReader r(mode->data_.data(), 4, "<MODE chunk>");
    r.readBytes(tag.data(), 4);
    if (tagsEqual(tag, kModeTagDense))
        return VoxelSetMode::DENSE;
    if (tagsEqual(tag, kModeTagShapes))
        return VoxelSetMode::SHAPES;
    if (tagsEqual(tag, kModeTagHybrid))
        return VoxelSetMode::HYBRID;
    IRE_LOG_WARN("readModeChunk: unknown mode tag '{}'", tagToString(tag));
    return VoxelSetMode::UNKNOWN;
}

// ---- SREF chunk --------------------------------------------------------

ChunkPayload makeShapeRefsChunk(std::span<const NameTableEntry> entries) {
    MemoryBinaryWriter w;
    const auto status = writeNameTable(w, entries);
    if (!status.ok()) {
        IRE_LOG_ERROR("makeShapeRefsChunk: writeNameTable failed: {}", status.message_);
        return ChunkPayload{kChunkTagShapeRefs, {}};
    }
    ChunkPayload out;
    out.tag_ = kChunkTagShapeRefs;
    out.data_ = w.takeBuffer();
    return out;
}

std::vector<NameTableEntry> buildCurrentShapeTypeNameTable() {
    std::vector<NameTableEntry> out;
    out.reserve(std::size(kShapeTypeTable));
    for (const auto &e : kShapeTypeTable) {
        out.push_back(NameTableEntry{e.id_, e.name_});
    }
    return out;
}

Result<std::vector<NameTableEntry>> readShapeRefsChunk(std::span<const std::uint8_t> body) {
    MemoryBinaryReader r(body.data(), body.size(), "<SREF chunk>");
    return readNameTable(r);
}

// ---- SHPG chunk --------------------------------------------------------

ChunkPayload makeShapeGroupChunk(std::span<const ShapeRecord> records) {
    MemoryBinaryWriter w;
    w.writeVarUInt(static_cast<std::uint64_t>(records.size()));
    for (const auto &r : records) {
        w.writeU32(r.shapeTypeId_);
        w.writeU16(r.recordVersion_);
        writeVec4(w, r.params_);
        writeColorPacked(w, r.color_);
        w.writeU32(r.flags_);
        w.writeU8(r.boneId_);
        writeVec3(w, r.offset_);
        writeVec4(w, r.rotation_);
        w.writeU8(static_cast<std::uint8_t>(r.csgOp_));
    }
    ChunkPayload out;
    out.tag_ = kChunkTagShapeGroup;
    out.data_ = w.takeBuffer();
    return out;
}

Result<ShapeGroupLoadResult>
readShapeGroupChunk(std::span<const std::uint8_t> body, const NameTable &diskShapeTypes) {
    MemoryBinaryReader r(body.data(), body.size(), "<SHPG chunk>");
    auto countR = r.readVarUInt();
    if (!countR.ok()) {
        return Result<ShapeGroupLoadResult>::error(
            countR.status_.code_,
            std::move(countR.status_.message_)
        );
    }
    // Cap the upfront reserve to remaining bytes so a corrupted count
    // claiming billions of records doesn't pre-allocate that much.
    const std::uint64_t cap = r.remaining();
    ShapeGroupLoadResult out;
    out.records_.reserve(static_cast<std::size_t>(countR.value_ < cap ? countR.value_ : cap));

    NameTable currentTable(buildCurrentShapeTypeNameTable());

    for (std::uint64_t i = 0; i < countR.value_; ++i) {
        ShapeRecord rec{};

        auto typeIdR = r.readU32();
        if (!typeIdR.ok())
            return Result<ShapeGroupLoadResult>::error(
                typeIdR.status_.code_,
                std::move(typeIdR.status_.message_)
            );
        const std::uint32_t diskTypeId = typeIdR.value_;

        auto verR = r.readU16();
        if (!verR.ok())
            return Result<ShapeGroupLoadResult>::error(
                verR.status_.code_,
                std::move(verR.status_.message_)
            );
        rec.recordVersion_ = verR.value_;

        auto paramsR = readVec4(r);
        if (!paramsR.ok())
            return Result<ShapeGroupLoadResult>::error(
                paramsR.status_.code_,
                std::move(paramsR.status_.message_)
            );
        rec.params_ = paramsR.value_;

        auto colorR = r.readU32();
        if (!colorR.ok())
            return Result<ShapeGroupLoadResult>::error(
                colorR.status_.code_,
                std::move(colorR.status_.message_)
            );
        rec.color_ = unpackColor(colorR.value_);

        auto flagsR = r.readU32();
        if (!flagsR.ok())
            return Result<ShapeGroupLoadResult>::error(
                flagsR.status_.code_,
                std::move(flagsR.status_.message_)
            );
        rec.flags_ = flagsR.value_;

        auto boneR = r.readU8();
        if (!boneR.ok())
            return Result<ShapeGroupLoadResult>::error(
                boneR.status_.code_,
                std::move(boneR.status_.message_)
            );
        rec.boneId_ = boneR.value_;

        auto offsetR = readVec3(r);
        if (!offsetR.ok())
            return Result<ShapeGroupLoadResult>::error(
                offsetR.status_.code_,
                std::move(offsetR.status_.message_)
            );
        rec.offset_ = offsetR.value_;

        auto rotR = readVec4(r);
        if (!rotR.ok())
            return Result<ShapeGroupLoadResult>::error(
                rotR.status_.code_,
                std::move(rotR.status_.message_)
            );
        rec.rotation_ = rotR.value_;

        auto csgR = r.readU8();
        if (!csgR.ok())
            return Result<ShapeGroupLoadResult>::error(
                csgR.status_.code_,
                std::move(csgR.status_.message_)
            );
        // CsgOp is stable — future composition modes land in a separate
        // SHPT chunk, not new CsgOp values, so no name-resolution fallback
        // is needed (unlike ShapeType, which uses SREF for Rule #2).
        rec.csgOp_ = static_cast<CsgOp>(csgR.value_);

        // Resolve the disk shape id to the current build's enum.
        // Rule #2: prefer name → current_enum; fall back to verbatim id
        // when the file has no SREF chunk (legacy save).
        if (diskShapeTypes.empty()) {
            rec.shapeTypeId_ = diskTypeId;
            out.records_.push_back(rec);
            continue;
        }
        auto diskName = diskShapeTypes.nameById(diskTypeId);
        if (!diskName.has_value()) {
            IRE_LOG_WARN(
                "readShapeGroupChunk: unknown shape ID={} (not in disk SREF), skipping",
                diskTypeId
            );
            ++out.unknownShapesSkipped_;
            continue;
        }
        auto currentId = currentTable.idByName(*diskName);
        if (!currentId.has_value()) {
            IRE_LOG_WARN(
                "readShapeGroupChunk: unknown shape ID={} name={}, skipping",
                diskTypeId,
                std::string(*diskName)
            );
            ++out.unknownShapesSkipped_;
            continue;
        }
        rec.shapeTypeId_ = *currentId;
        out.records_.push_back(rec);
    }
    return Result<ShapeGroupLoadResult>::success(std::move(out));
}

// ---- High-level save/load ---------------------------------------------

BinaryStatus saveShapeGroup(const std::string &path, std::span<const ShapeRecord> records) {
    FileBinaryWriter fw(path);
    if (!fw.ok()) {
        return BinaryStatus::error(
            BinaryIOError::OpenFailed,
            "saveShapeGroup: could not open '" + path + "' for write"
        );
    }
    const auto shapeTypeEntries = buildCurrentShapeTypeNameTable();
    std::array<ChunkPayload, 3> chunks = {
        makeModeChunk(VoxelSetMode::SHAPES),
        makeShapeRefsChunk(shapeTypeEntries),
        makeShapeGroupChunk(records),
    };
    return writeChunked(fw, kVoxelSetMagic, kVoxelSetVersion, chunks);
}

Result<VoxelSetFile> loadShapeGroup(const std::string &path) {
    FileBinaryReader fr(path);
    if (!fr.ok()) {
        return Result<VoxelSetFile>::error(
            BinaryIOError::OpenFailed,
            "loadShapeGroup: could not open '" + path + "' for read"
        );
    }
    auto chunksR = readChunks(fr, kVoxelSetMagic, kVoxelSetVersion);
    if (!chunksR.ok()) {
        return Result<VoxelSetFile>::error(
            chunksR.status_.code_,
            std::move(chunksR.status_.message_)
        );
    }
    VoxelSetFile out;
    out.mode_ = readModeChunk(chunksR.value_);

    NameTable diskShapeTypes;
    if (const LoadedChunk *sref = findChunk(chunksR.value_, kChunkTagShapeRefs)) {
        auto entriesR = readShapeRefsChunk(sref->data_);
        if (!entriesR.ok()) {
            return Result<VoxelSetFile>::error(
                entriesR.status_.code_,
                std::move(entriesR.status_.message_)
            );
        }
        diskShapeTypes = NameTable(std::move(entriesR.value_));
    }

    if (const LoadedChunk *shpg = findChunk(chunksR.value_, kChunkTagShapeGroup)) {
        auto recR = readShapeGroupChunk(shpg->data_, diskShapeTypes);
        if (!recR.ok()) {
            return Result<VoxelSetFile>::error(
                recR.status_.code_,
                std::move(recR.status_.message_)
            );
        }
        out.shapeRecords_ = std::move(recR.value_.records_);
        out.unknownShapesSkipped_ = recR.value_.unknownShapesSkipped_;
    }
    return Result<VoxelSetFile>::success(std::move(out));
}

} // namespace IRAsset
