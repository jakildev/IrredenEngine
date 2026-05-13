#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/asset/json_sidecar.hpp>

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

// Emit a .vxs.json sidecar alongside the binary. `dense` is null for
// SHAPES-only saves; `shapes` is empty for DENSE-only saves.
void emitVoxelSetSidecar(
    const std::string &path,
    VoxelSetMode mode,
    const DenseVoxelSet *dense,
    std::span<const ShapeRecord> shapes
) {
    const std::string sidecarPath = path + ".json";
    JsonSidecarWriter j;
    j.beginObject();

    j.key("version");
    j.valueUInt(kVoxelSetVersion);

    const char *modeStr = "UNKNOWN";
    switch (mode) {
    case VoxelSetMode::DENSE:
        modeStr = "DENSE";
        break;
    case VoxelSetMode::SHAPES:
        modeStr = "SHAPES";
        break;
    case VoxelSetMode::HYBRID:
        modeStr = "HYBRID";
        break;
    default:
        break;
    }
    j.key("mode");
    j.valueString(modeStr);

    if (dense != nullptr) {
        j.key("bounds");
        j.beginObject();
        j.key("min");
        j.beginArray();
        j.valueInt(dense->boundsMin_.x);
        j.valueInt(dense->boundsMin_.y);
        j.valueInt(dense->boundsMin_.z);
        j.endArray();
        j.key("max");
        j.beginArray();
        j.valueInt(dense->boundsMax_.x);
        j.valueInt(dense->boundsMax_.y);
        j.valueInt(dense->boundsMax_.z);
        j.endArray();
        j.endObject();
    } else {
        j.key("bounds");
        j.valueNull();
    }

    j.key("material_registry_refs");
    j.beginArray();
    j.endArray();

    j.key("layer_names");
    j.beginArray();
    if (dense != nullptr) {
        for (const auto &layer : dense->layers_) {
            j.valueString(layer.name_);
        }
    }
    j.endArray();

    const std::uint64_t frameCount =
        (dense != nullptr) ? static_cast<std::uint64_t>(dense->frames_.size()) : 0u;
    j.key("frame_count");
    j.valueUInt(frameCount);

    // Count shapes per type using kShapeTypeTable for deterministic ordering.
    j.key("shape_primitives_summary");
    j.beginObject();
    for (const auto &entry : kShapeTypeTable) {
        std::size_t count = 0;
        for (const auto &rec : shapes) {
            if (rec.shapeTypeId_ == entry.id_)
                ++count;
        }
        if (count > 0) {
            j.key(entry.name_);
            j.valueUInt(static_cast<std::uint64_t>(count));
        }
    }
    j.endObject();

    j.endObject();
    writeJsonSidecarToFile(sidecarPath, j.str());
}

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

// ---- BNDS chunk -------------------------------------------------------

ChunkPayload makeBoundsChunk(const ivec3 &boundsMin, const ivec3 &boundsMax) {
    MemoryBinaryWriter w;
    w.writeI32(boundsMin.x);
    w.writeI32(boundsMin.y);
    w.writeI32(boundsMin.z);
    w.writeI32(boundsMax.x);
    w.writeI32(boundsMax.y);
    w.writeI32(boundsMax.z);
    ChunkPayload out;
    out.tag_ = kChunkTagBounds;
    out.data_ = w.takeBuffer();
    return out;
}

Result<BoundsPair> readBoundsChunk(std::span<const std::uint8_t> body) {
    MemoryBinaryReader r(body.data(), body.size(), "<BNDS chunk>");
    BoundsPair out;
    auto minX = r.readI32();
    if (!minX.ok())
        return Result<BoundsPair>::error(minX.status_.code_, std::move(minX.status_.message_));
    auto minY = r.readI32();
    if (!minY.ok())
        return Result<BoundsPair>::error(minY.status_.code_, std::move(minY.status_.message_));
    auto minZ = r.readI32();
    if (!minZ.ok())
        return Result<BoundsPair>::error(minZ.status_.code_, std::move(minZ.status_.message_));
    auto maxX = r.readI32();
    if (!maxX.ok())
        return Result<BoundsPair>::error(maxX.status_.code_, std::move(maxX.status_.message_));
    auto maxY = r.readI32();
    if (!maxY.ok())
        return Result<BoundsPair>::error(maxY.status_.code_, std::move(maxY.status_.message_));
    auto maxZ = r.readI32();
    if (!maxZ.ok())
        return Result<BoundsPair>::error(maxZ.status_.code_, std::move(maxZ.status_.message_));
    out.boundsMin_ = ivec3(minX.value_, minY.value_, minZ.value_);
    out.boundsMax_ = ivec3(maxX.value_, maxY.value_, maxZ.value_);
    return Result<BoundsPair>::success(out);
}

// ---- VOXR chunk -------------------------------------------------------

ChunkPayload makeVoxelRecordsChunk(std::span<const VoxelRecord> voxels) {
    MemoryBinaryWriter w;
    w.writeU16(VoxelRecord::kSaveVersion);
    w.writeVarUInt(static_cast<std::uint64_t>(voxels.size()));
    for (const auto &v : voxels) {
        writeColorPacked(w, v.color_);
        w.writeU8(v.material_id_);
        w.writeU8(v.flags_);
        w.writeU8(v.bone_id_);
        w.writeU8(v.pad0_);
        w.writeU32(v.reserved_);
    }
    ChunkPayload out;
    out.tag_ = kChunkTagVoxelRecords;
    out.data_ = w.takeBuffer();
    return out;
}

Result<VoxelRecordsLoadResult>
readVoxelRecordsChunk(std::span<const std::uint8_t> body, std::size_t expectedCount) {
    MemoryBinaryReader r(body.data(), body.size(), "<VOXR chunk>");
    auto verR = r.readU16();
    if (!verR.ok()) {
        return Result<VoxelRecordsLoadResult>::error(
            verR.status_.code_,
            std::move(verR.status_.message_)
        );
    }
    auto countR = r.readVarUInt();
    if (!countR.ok()) {
        return Result<VoxelRecordsLoadResult>::error(
            countR.status_.code_,
            std::move(countR.status_.message_)
        );
    }
    if (countR.value_ != static_cast<std::uint64_t>(expectedCount)) {
        IRE_LOG_WARN(
            "readVoxelRecordsChunk: chunk count={} != bounds-derived count={}",
            countR.value_,
            expectedCount
        );
    }
    // Cap upfront reserve to remaining bytes / record size (12 B) so a
    // corrupted count claiming billions of records doesn't pre-allocate.
    constexpr std::uint64_t kRecordBytes = 12;
    const std::uint64_t maxRecords = r.remaining() / kRecordBytes;
    VoxelRecordsLoadResult out;
    out.recordVersion_ = verR.value_;
    out.voxels_.reserve(
        static_cast<std::size_t>(countR.value_ < maxRecords ? countR.value_ : maxRecords)
    );
    for (std::uint64_t i = 0; i < countR.value_; ++i) {
        VoxelRecord v{};
        auto colorR = r.readU32();
        if (!colorR.ok())
            return Result<VoxelRecordsLoadResult>::error(
                colorR.status_.code_,
                std::move(colorR.status_.message_)
            );
        v.color_ = unpackColor(colorR.value_);
        auto matR = r.readU8();
        if (!matR.ok())
            return Result<VoxelRecordsLoadResult>::error(
                matR.status_.code_,
                std::move(matR.status_.message_)
            );
        v.material_id_ = matR.value_;
        auto flagsR = r.readU8();
        if (!flagsR.ok())
            return Result<VoxelRecordsLoadResult>::error(
                flagsR.status_.code_,
                std::move(flagsR.status_.message_)
            );
        v.flags_ = flagsR.value_;
        auto boneR = r.readU8();
        if (!boneR.ok())
            return Result<VoxelRecordsLoadResult>::error(
                boneR.status_.code_,
                std::move(boneR.status_.message_)
            );
        v.bone_id_ = boneR.value_;
        auto padR = r.readU8();
        if (!padR.ok())
            return Result<VoxelRecordsLoadResult>::error(
                padR.status_.code_,
                std::move(padR.status_.message_)
            );
        v.pad0_ = padR.value_;
        auto resR = r.readU32();
        if (!resR.ok())
            return Result<VoxelRecordsLoadResult>::error(
                resR.status_.code_,
                std::move(resR.status_.message_)
            );
        v.reserved_ = resR.value_;
        out.voxels_.push_back(v);
    }
    return Result<VoxelRecordsLoadResult>::success(std::move(out));
}

// ---- LAYR chunk -------------------------------------------------------

ChunkPayload makeLayersChunk(std::span<const LayerInfo> layers) {
    MemoryBinaryWriter w;
    w.writeVarUInt(static_cast<std::uint64_t>(layers.size()));
    for (const auto &layer : layers) {
        w.writeString(layer.name_);
        w.writeVarUInt(static_cast<std::uint64_t>(layer.bitmask_.size()));
        for (std::uint64_t word : layer.bitmask_) {
            w.writeU64(word);
        }
    }
    ChunkPayload out;
    out.tag_ = kChunkTagLayers;
    out.data_ = w.takeBuffer();
    return out;
}

Result<std::vector<LayerInfo>> readLayersChunk(std::span<const std::uint8_t> body) {
    MemoryBinaryReader r(body.data(), body.size(), "<LAYR chunk>");
    auto countR = r.readVarUInt();
    if (!countR.ok()) {
        return Result<std::vector<LayerInfo>>::error(
            countR.status_.code_,
            std::move(countR.status_.message_)
        );
    }
    std::vector<LayerInfo> out;
    const std::uint64_t cap = r.remaining();
    out.reserve(static_cast<std::size_t>(countR.value_ < cap ? countR.value_ : cap));
    for (std::uint64_t i = 0; i < countR.value_; ++i) {
        LayerInfo layer;
        auto nameR = r.readString();
        if (!nameR.ok())
            return Result<std::vector<LayerInfo>>::error(
                nameR.status_.code_,
                std::move(nameR.status_.message_)
            );
        layer.name_ = std::move(nameR.value_);
        auto wordCountR = r.readVarUInt();
        if (!wordCountR.ok())
            return Result<std::vector<LayerInfo>>::error(
                wordCountR.status_.code_,
                std::move(wordCountR.status_.message_)
            );
        // Cap u64 word count by remaining bytes / 8 to defuse a corrupted
        // count that claims billions of words.
        const std::uint64_t maxWords = r.remaining() / 8;
        layer.bitmask_.reserve(
            static_cast<std::size_t>(wordCountR.value_ < maxWords ? wordCountR.value_ : maxWords)
        );
        for (std::uint64_t w_i = 0; w_i < wordCountR.value_; ++w_i) {
            auto wordR = r.readU64();
            if (!wordR.ok())
                return Result<std::vector<LayerInfo>>::error(
                    wordR.status_.code_,
                    std::move(wordR.status_.message_)
                );
            layer.bitmask_.push_back(wordR.value_);
        }
        out.push_back(std::move(layer));
    }
    return Result<std::vector<LayerInfo>>::success(std::move(out));
}

// ---- FRAM chunk -------------------------------------------------------

ChunkPayload makeFramesChunk(std::span<const FramePose> frames) {
    MemoryBinaryWriter w;
    w.writeVarUInt(static_cast<std::uint64_t>(frames.size()));
    for (const auto &frame : frames) {
        w.writeU32(frame.frameIndex_);
        w.writeVarUInt(static_cast<std::uint64_t>(frame.offsets_.size()));
        for (const auto &off : frame.offsets_) {
            writeVec3(w, off);
        }
    }
    ChunkPayload out;
    out.tag_ = kChunkTagFrames;
    out.data_ = w.takeBuffer();
    return out;
}

Result<FramesLoadResult>
readFramesChunk(std::span<const std::uint8_t> body, std::size_t voxelCount) {
    MemoryBinaryReader r(body.data(), body.size(), "<FRAM chunk>");
    auto frameCountR = r.readVarUInt();
    if (!frameCountR.ok()) {
        return Result<FramesLoadResult>::error(
            frameCountR.status_.code_,
            std::move(frameCountR.status_.message_)
        );
    }
    FramesLoadResult out;
    const std::uint64_t cap = r.remaining();
    out.frames_.reserve(
        static_cast<std::size_t>(frameCountR.value_ < cap ? frameCountR.value_ : cap)
    );
    for (std::uint64_t i = 0; i < frameCountR.value_; ++i) {
        auto frameIdxR = r.readU32();
        if (!frameIdxR.ok())
            return Result<FramesLoadResult>::error(
                frameIdxR.status_.code_,
                std::move(frameIdxR.status_.message_)
            );
        auto offCountR = r.readVarUInt();
        if (!offCountR.ok())
            return Result<FramesLoadResult>::error(
                offCountR.status_.code_,
                std::move(offCountR.status_.message_)
            );
        FramePose frame;
        frame.frameIndex_ = frameIdxR.value_;
        // Cap reserve by remaining bytes / 12 B per vec3.
        constexpr std::uint64_t kVec3Bytes = 12;
        const std::uint64_t maxOffsets = r.remaining() / kVec3Bytes;
        frame.offsets_.reserve(
            static_cast<std::size_t>(offCountR.value_ < maxOffsets ? offCountR.value_ : maxOffsets)
        );
        for (std::uint64_t off_i = 0; off_i < offCountR.value_; ++off_i) {
            auto vR = readVec3(r);
            if (!vR.ok())
                return Result<FramesLoadResult>::error(
                    vR.status_.code_,
                    std::move(vR.status_.message_)
                );
            frame.offsets_.push_back(vR.value_);
        }
        if (frame.offsets_.size() != voxelCount) {
            IRE_LOG_WARN(
                "readFramesChunk: frame {} has {} offsets, expected {} (dropping)",
                frame.frameIndex_,
                frame.offsets_.size(),
                voxelCount
            );
            ++out.skippedFrames_;
            continue;
        }
        out.frames_.push_back(std::move(frame));
    }
    return Result<FramesLoadResult>::success(std::move(out));
}

// ---- META chunk -------------------------------------------------------

ChunkPayload makeMetaChunk(std::span<const MetaEntry> entries) {
    MemoryBinaryWriter w;
    w.writeVarUInt(static_cast<std::uint64_t>(entries.size()));
    for (const auto &entry : entries) {
        w.writeString(entry.key_);
        w.writeString(entry.value_);
    }
    ChunkPayload out;
    out.tag_ = kChunkTagMeta;
    out.data_ = w.takeBuffer();
    return out;
}

Result<std::vector<MetaEntry>> readMetaChunk(std::span<const std::uint8_t> body) {
    MemoryBinaryReader r(body.data(), body.size(), "<META chunk>");
    auto countR = r.readVarUInt();
    if (!countR.ok()) {
        return Result<std::vector<MetaEntry>>::error(
            countR.status_.code_,
            std::move(countR.status_.message_)
        );
    }
    std::vector<MetaEntry> out;
    const std::uint64_t cap = r.remaining();
    out.reserve(static_cast<std::size_t>(countR.value_ < cap ? countR.value_ : cap));
    for (std::uint64_t i = 0; i < countR.value_; ++i) {
        MetaEntry entry;
        auto keyR = r.readString();
        if (!keyR.ok())
            return Result<std::vector<MetaEntry>>::error(
                keyR.status_.code_,
                std::move(keyR.status_.message_)
            );
        entry.key_ = std::move(keyR.value_);
        auto valR = r.readString();
        if (!valR.ok())
            return Result<std::vector<MetaEntry>>::error(
                valR.status_.code_,
                std::move(valR.status_.message_)
            );
        entry.value_ = std::move(valR.value_);
        out.push_back(std::move(entry));
    }
    return Result<std::vector<MetaEntry>>::success(std::move(out));
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

BinaryStatus saveDenseVoxelSet(const std::string &path, const DenseVoxelSet &dense) {
    FileBinaryWriter fw(path);
    if (!fw.ok()) {
        return BinaryStatus::error(
            BinaryIOError::OpenFailed,
            "saveDenseVoxelSet: could not open '" + path + "' for write"
        );
    }
    // Build the chunk set in order: MODE, BNDS, VOXR (always); LAYR /
    // FRAM / META only if the caller has data (Rule #1 — extra chunks
    // are additive, missing chunks degrade to empty on load).
    std::vector<ChunkPayload> chunks;
    chunks.reserve(6);
    chunks.push_back(makeModeChunk(VoxelSetMode::DENSE));
    chunks.push_back(makeBoundsChunk(dense.boundsMin_, dense.boundsMax_));
    chunks.push_back(makeVoxelRecordsChunk(dense.voxels_));
    if (!dense.layers_.empty()) {
        chunks.push_back(makeLayersChunk(dense.layers_));
    }
    if (!dense.frames_.empty()) {
        chunks.push_back(makeFramesChunk(dense.frames_));
    }
    if (!dense.meta_.empty()) {
        chunks.push_back(makeMetaChunk(dense.meta_));
    }
    return writeChunked(fw, kVoxelSetMagic, kVoxelSetVersion, chunks);
}

Result<DenseVoxelSetFile> loadDenseVoxelSet(const std::string &path) {
    FileBinaryReader fr(path);
    if (!fr.ok()) {
        return Result<DenseVoxelSetFile>::error(
            BinaryIOError::OpenFailed,
            "loadDenseVoxelSet: could not open '" + path + "' for read"
        );
    }
    auto chunksR = readChunks(fr, kVoxelSetMagic, kVoxelSetVersion);
    if (!chunksR.ok()) {
        return Result<DenseVoxelSetFile>::error(
            chunksR.status_.code_,
            std::move(chunksR.status_.message_)
        );
    }
    DenseVoxelSetFile out;
    out.mode_ = readModeChunk(chunksR.value_);

    // BNDS must be present for DENSE mode to populate; absent BNDS
    // means this is a SHAPES-only save (the SHPG branch above stays
    // empty in the dense file view).
    const LoadedChunk *bndsChunk = findChunk(chunksR.value_, kChunkTagBounds);
    if (bndsChunk == nullptr) {
        return Result<DenseVoxelSetFile>::success(std::move(out));
    }
    auto boundsR = readBoundsChunk(bndsChunk->data_);
    if (!boundsR.ok()) {
        return Result<DenseVoxelSetFile>::error(
            boundsR.status_.code_,
            std::move(boundsR.status_.message_)
        );
    }
    out.dense_.boundsMin_ = boundsR.value_.boundsMin_;
    out.dense_.boundsMax_ = boundsR.value_.boundsMax_;
    const std::size_t expectedCount = out.dense_.voxelCount();

    if (const LoadedChunk *voxr = findChunk(chunksR.value_, kChunkTagVoxelRecords)) {
        auto vR = readVoxelRecordsChunk(voxr->data_, expectedCount);
        if (!vR.ok()) {
            return Result<DenseVoxelSetFile>::error(
                vR.status_.code_,
                std::move(vR.status_.message_)
            );
        }
        out.dense_.recordVersion_ = vR.value_.recordVersion_;
        out.dense_.voxels_ = std::move(vR.value_.voxels_);
    } else if (expectedCount > 0) {
        // BNDS present but VOXR absent → malformed save. Rule #5
        // requires we proceed (caller may still want the bounds), but
        // log loudly so the caller knows `voxels_` is empty while
        // `voxelCount()` reports a positive number.
        IRE_LOG_WARN(
            "loadDenseVoxelSet: BNDS present (voxelCount={}) but VOXR "
            "chunk missing in '{}'; voxels_ left empty",
            expectedCount,
            path
        );
    }

    if (const LoadedChunk *layr = findChunk(chunksR.value_, kChunkTagLayers)) {
        auto lR = readLayersChunk(layr->data_);
        if (!lR.ok()) {
            return Result<DenseVoxelSetFile>::error(
                lR.status_.code_,
                std::move(lR.status_.message_)
            );
        }
        out.dense_.layers_ = std::move(lR.value_);
    }

    if (const LoadedChunk *fram = findChunk(chunksR.value_, kChunkTagFrames)) {
        auto fR = readFramesChunk(fram->data_, expectedCount);
        if (!fR.ok()) {
            return Result<DenseVoxelSetFile>::error(
                fR.status_.code_,
                std::move(fR.status_.message_)
            );
        }
        out.dense_.frames_ = std::move(fR.value_.frames_);
        out.skippedFrames_ = fR.value_.skippedFrames_;
    }

    if (const LoadedChunk *meta = findChunk(chunksR.value_, kChunkTagMeta)) {
        auto mR = readMetaChunk(meta->data_);
        if (!mR.ok()) {
            return Result<DenseVoxelSetFile>::error(
                mR.status_.code_,
                std::move(mR.status_.message_)
            );
        }
        out.dense_.meta_ = std::move(mR.value_);
    }
    return Result<DenseVoxelSetFile>::success(std::move(out));
}

BinaryStatus saveVoxelSet(
    const std::string &path, std::span<const ShapeRecord> shapes, const DenseVoxelSet &dense
) {
    FileBinaryWriter fw(path);
    if (!fw.ok()) {
        return BinaryStatus::error(
            BinaryIOError::OpenFailed,
            "saveVoxelSet: could not open '" + path + "' for write"
        );
    }
    const auto shapeTypeEntries = buildCurrentShapeTypeNameTable();
    std::vector<ChunkPayload> chunks;
    chunks.reserve(8);
    chunks.push_back(makeModeChunk(VoxelSetMode::HYBRID));
    chunks.push_back(makeShapeRefsChunk(shapeTypeEntries));
    chunks.push_back(makeShapeGroupChunk(shapes));
    chunks.push_back(makeBoundsChunk(dense.boundsMin_, dense.boundsMax_));
    chunks.push_back(makeVoxelRecordsChunk(dense.voxels_));
    if (!dense.layers_.empty()) {
        chunks.push_back(makeLayersChunk(dense.layers_));
    }
    if (!dense.frames_.empty()) {
        chunks.push_back(makeFramesChunk(dense.frames_));
    }
    if (!dense.meta_.empty()) {
        chunks.push_back(makeMetaChunk(dense.meta_));
    }
    const auto status = writeChunked(fw, kVoxelSetMagic, kVoxelSetVersion, chunks);
    if (status.ok()) {
        emitVoxelSetSidecar(path, VoxelSetMode::HYBRID, &dense, shapes);
    }
    return status;
}

Result<VoxelSetAllFile> loadVoxelSet(const std::string &path) {
    FileBinaryReader fr(path);
    if (!fr.ok()) {
        return Result<VoxelSetAllFile>::error(
            BinaryIOError::OpenFailed,
            "loadVoxelSet: could not open '" + path + "' for read"
        );
    }
    auto chunksR = readChunks(fr, kVoxelSetMagic, kVoxelSetVersion);
    if (!chunksR.ok()) {
        return Result<VoxelSetAllFile>::error(
            chunksR.status_.code_,
            std::move(chunksR.status_.message_)
        );
    }
    VoxelSetAllFile out;
    out.mode_ = readModeChunk(chunksR.value_);

    // Shape half
    NameTable diskShapeTypes;
    if (const LoadedChunk *sref = findChunk(chunksR.value_, kChunkTagShapeRefs)) {
        auto entriesR = readShapeRefsChunk(sref->data_);
        if (!entriesR.ok()) {
            return Result<VoxelSetAllFile>::error(
                entriesR.status_.code_,
                std::move(entriesR.status_.message_)
            );
        }
        diskShapeTypes = NameTable(std::move(entriesR.value_));
    }
    if (const LoadedChunk *shpg = findChunk(chunksR.value_, kChunkTagShapeGroup)) {
        auto recR = readShapeGroupChunk(shpg->data_, diskShapeTypes);
        if (!recR.ok()) {
            return Result<VoxelSetAllFile>::error(
                recR.status_.code_,
                std::move(recR.status_.message_)
            );
        }
        out.shapeRecords_ = std::move(recR.value_.records_);
        out.unknownShapesSkipped_ = recR.value_.unknownShapesSkipped_;
    }

    // Dense half — absent BNDS means no dense data.
    const LoadedChunk *bndsChunk = findChunk(chunksR.value_, kChunkTagBounds);
    if (bndsChunk == nullptr) {
        return Result<VoxelSetAllFile>::success(std::move(out));
    }
    auto boundsR = readBoundsChunk(bndsChunk->data_);
    if (!boundsR.ok()) {
        return Result<VoxelSetAllFile>::error(
            boundsR.status_.code_,
            std::move(boundsR.status_.message_)
        );
    }
    out.dense_.boundsMin_ = boundsR.value_.boundsMin_;
    out.dense_.boundsMax_ = boundsR.value_.boundsMax_;
    const std::size_t expectedCount = out.dense_.voxelCount();

    if (const LoadedChunk *voxr = findChunk(chunksR.value_, kChunkTagVoxelRecords)) {
        auto vR = readVoxelRecordsChunk(voxr->data_, expectedCount);
        if (!vR.ok()) {
            return Result<VoxelSetAllFile>::error(vR.status_.code_, std::move(vR.status_.message_));
        }
        out.dense_.recordVersion_ = vR.value_.recordVersion_;
        out.dense_.voxels_ = std::move(vR.value_.voxels_);
    } else if (expectedCount > 0) {
        IRE_LOG_WARN(
            "loadVoxelSet: BNDS present (voxelCount={}) but VOXR "
            "chunk missing in '{}'; voxels_ left empty",
            expectedCount,
            path
        );
    }

    if (const LoadedChunk *layr = findChunk(chunksR.value_, kChunkTagLayers)) {
        auto lR = readLayersChunk(layr->data_);
        if (!lR.ok()) {
            return Result<VoxelSetAllFile>::error(lR.status_.code_, std::move(lR.status_.message_));
        }
        out.dense_.layers_ = std::move(lR.value_);
    }

    if (const LoadedChunk *fram = findChunk(chunksR.value_, kChunkTagFrames)) {
        auto fR = readFramesChunk(fram->data_, expectedCount);
        if (!fR.ok()) {
            return Result<VoxelSetAllFile>::error(fR.status_.code_, std::move(fR.status_.message_));
        }
        out.dense_.frames_ = std::move(fR.value_.frames_);
        out.skippedFrames_ = fR.value_.skippedFrames_;
    }

    if (const LoadedChunk *meta = findChunk(chunksR.value_, kChunkTagMeta)) {
        auto mR = readMetaChunk(meta->data_);
        if (!mR.ok()) {
            return Result<VoxelSetAllFile>::error(mR.status_.code_, std::move(mR.status_.message_));
        }
        out.dense_.meta_ = std::move(mR.value_);
    }

    return Result<VoxelSetAllFile>::success(std::move(out));
}

} // namespace IRAsset
