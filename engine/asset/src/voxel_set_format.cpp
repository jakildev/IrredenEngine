#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/asset/json_sidecar.hpp>
#include <irreden/asset/math_binary_io.hpp>

#include <irreden/ir_profile.hpp>

#include <utility>

namespace IRAsset {

namespace {

// JSON key literals used by emitVoxelSetSidecar. Anchored here so
// future sidecar emitters can grep for a single constant name and
// find the canonical spelling across all asset formats.
constexpr std::string_view kSidecarKeyVersion = "version";
constexpr std::string_view kSidecarKeyMode = "mode";
constexpr std::string_view kSidecarKeyBounds = "bounds";
constexpr std::string_view kSidecarKeyBoundsMin = "min";
constexpr std::string_view kSidecarKeyBoundsMax = "max";
constexpr std::string_view kSidecarKeyMaterialRegistryRefs = "material_registry_refs";
constexpr std::string_view kSidecarKeyLayerNames = "layer_names";
constexpr std::string_view kSidecarKeyFrameCount = "frame_count";
constexpr std::string_view kSidecarKeyShapePrimitivesSummary = "shape_primitives_summary";

// Canonical (id, name) table for `IRMath::SDF::ShapeType`. Keep in
// lockstep with the enum declaration. Adding a value here is the
// "extends an enum across save format" step — older builds receiving
// the new save look up by name, fail, and skip that record.
struct ShapeTypeNameEntry {
    std::uint32_t id_;
    const char *name_;
};
// On-disk size of one packed VoxelRecord; caps reserve() against a corrupt count.
constexpr std::uint64_t kRecordBytes = 12;
// On-disk size of one vec3 (3 × f32); caps frame-offset reserve() against a corrupt count.
constexpr std::uint64_t kVec3Bytes = 12;

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

// Emit a .vxs.json sidecar alongside the binary. Three call paths:
//   - SHAPES save:  dense=nullptr, shapes=records (bounds emit as null).
//   - DENSE save:   dense=&dense,  shapes={}      (shape_primitives_summary empty).
//   - HYBRID save:  dense=&dense,  shapes=records (both halves populated).
void emitVoxelSetSidecar(
    const std::string &path,
    VoxelSetMode mode,
    const DenseVoxelSet *dense,
    std::span<const ShapeRecord> shapes
) {
    const std::string sidecarPath = path + ".json";
    JsonSidecarWriter j;
    j.beginObject();

    j.key(kSidecarKeyVersion);
    j.valueUInt(kVoxelSetVersion);

    j.key(kSidecarKeyMode);
    j.valueString(voxelSetModeToString(mode));

    if (dense != nullptr) {
        j.key(kSidecarKeyBounds);
        j.beginObject();
        j.key(kSidecarKeyBoundsMin);
        j.beginArray();
        j.valueInt(dense->boundsMin_.x);
        j.valueInt(dense->boundsMin_.y);
        j.valueInt(dense->boundsMin_.z);
        j.endArray();
        j.key(kSidecarKeyBoundsMax);
        j.beginArray();
        j.valueInt(dense->boundsMax_.x);
        j.valueInt(dense->boundsMax_.y);
        j.valueInt(dense->boundsMax_.z);
        j.endArray();
        j.endObject();
    } else {
        j.key(kSidecarKeyBounds);
        j.valueNull();
    }

    j.key(kSidecarKeyMaterialRegistryRefs);
    j.beginArray();
    j.endArray();

    j.key(kSidecarKeyLayerNames);
    j.beginArray();
    if (dense != nullptr) {
        for (const auto &layer : dense->layers_) {
            j.valueString(layer.name_);
        }
    }
    j.endArray();

    const std::uint64_t frameCount =
        (dense != nullptr) ? static_cast<std::uint64_t>(dense->frames_.size()) : 0u;
    j.key(kSidecarKeyFrameCount);
    j.valueUInt(frameCount);

    // Count shapes per type using kShapeTypeTable for deterministic ordering.
    j.key(kSidecarKeyShapePrimitivesSummary);
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

// ---- Shared load/validate helpers --------------------------------------
//
// The chunk readers below share a few mechanical patterns: forwarding a
// sub-read's error into the surrounding Result type, capping a corrupted
// declared-count against bytes-remaining so a bogus varuint can't
// pre-allocate gigabytes, decoding the 12 B VoxelRecord body, and
// dispatching VRLE-preferred / VOXR-fallback load into a DenseVoxelSet.

// Mirrors Rust's `?` — rebuild the source error in a different Result type
// so a sub-read failure surfaces with the right outer type.
template <typename TOut, typename TIn> Result<TOut> forwardError(Result<TIn> &source) {
    return Result<TOut>::error(source.status_.code_, std::move(source.status_.message_));
}

template <typename TOut> Result<TOut> forwardError(BinaryStatus &source) {
    return Result<TOut>::error(source.code_, std::move(source.message_));
}

// Cap a corrupted declared count to bytes-remaining so a malformed varuint
// claiming billions of records can't pre-allocate that much.
constexpr std::size_t cappedReserve(std::uint64_t requested, std::uint64_t cap) {
    return static_cast<std::size_t>(requested < cap ? requested : cap);
}

// Write the 12 B VoxelRecord body (matches VOXR / VRLE record layout —
// `Color` is packed RGBA, then 4 × u8 fields, then u32 reserved).
void writeVoxelRecordBody(BinaryWriter &w, const VoxelRecord &v) {
    IRMath::BinaryIO::writeColorPacked(w, v.color_);
    w.writeU8(v.material_id_);
    w.writeU8(v.flags_);
    w.writeU8(v.bone_id_);
    w.writeU8(v.layer_id_);
    w.writeU32(v.reserved_);
}

// Read the 12 B VoxelRecord body. Caller forwards any error to its own
// Result type via `forwardError<TOuter>(vR)`.
Result<VoxelRecord> readVoxelRecordBody(BinaryReader &r) {
    VoxelRecord v{};
    auto colorR = r.readU32();
    if (!colorR.ok())
        return forwardError<VoxelRecord>(colorR);
    v.color_ = Color::fromPackedRGBA(colorR.value_);
    auto matR = r.readU8();
    if (!matR.ok())
        return forwardError<VoxelRecord>(matR);
    v.material_id_ = matR.value_;
    auto flagsR = r.readU8();
    if (!flagsR.ok())
        return forwardError<VoxelRecord>(flagsR);
    v.flags_ = flagsR.value_;
    auto boneR = r.readU8();
    if (!boneR.ok())
        return forwardError<VoxelRecord>(boneR);
    v.bone_id_ = boneR.value_;
    auto layerR = r.readU8();
    if (!layerR.ok())
        return forwardError<VoxelRecord>(layerR);
    // v1 files had pad0_=0 here; v1 → v2 migration: treat as layer_id_=0
    // (default layer), which is the correct semantic for pre-layer files.
    v.layer_id_ = layerR.value_;
    auto resR = r.readU32();
    if (!resR.ok())
        return forwardError<VoxelRecord>(resR);
    v.reserved_ = resR.value_;
    return Result<VoxelRecord>::success(std::move(v));
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
    w.writeTag(tag);
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
    MemoryBinaryReader r(mode->data_.data(), 4, "<MODE chunk>");
    auto tagResult = r.readTag();
    if (!tagResult.ok()) {
        IRE_LOG_ERROR("readModeChunk: failed to read tag: {}", tagResult.status_.message_);
        return VoxelSetMode::UNKNOWN;
    }
    const auto &tag = tagResult.value_;
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
        IRMath::BinaryIO::writeVec4(w, r.params_);
        IRMath::BinaryIO::writeColorPacked(w, r.color_);
        w.writeU32(r.flags_);
        w.writeU8(r.boneId_);
        IRMath::BinaryIO::writeVec3(w, r.offset_);
        IRMath::BinaryIO::writeVec4(w, r.rotation_);
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
    if (!countR.ok())
        return forwardError<ShapeGroupLoadResult>(countR);
    ShapeGroupLoadResult out;
    out.records_.reserve(cappedReserve(countR.value_, r.remaining()));

    NameTable currentTable(buildCurrentShapeTypeNameTable());

    for (std::uint64_t i = 0; i < countR.value_; ++i) {
        ShapeRecord rec{};

        auto typeIdR = r.readU32();
        if (!typeIdR.ok())
            return forwardError<ShapeGroupLoadResult>(typeIdR);
        const std::uint32_t diskTypeId = typeIdR.value_;

        auto verR = r.readU16();
        if (!verR.ok())
            return forwardError<ShapeGroupLoadResult>(verR);
        rec.recordVersion_ = verR.value_;

        auto paramsR = IRMath::BinaryIO::readVec4(r);
        if (!paramsR.ok())
            return forwardError<ShapeGroupLoadResult>(paramsR);
        rec.params_ = paramsR.value_;

        auto colorR = r.readU32();
        if (!colorR.ok())
            return forwardError<ShapeGroupLoadResult>(colorR);
        rec.color_ = Color::fromPackedRGBA(colorR.value_);

        auto flagsR = r.readU32();
        if (!flagsR.ok())
            return forwardError<ShapeGroupLoadResult>(flagsR);
        rec.flags_ = flagsR.value_;

        auto boneR = r.readU8();
        if (!boneR.ok())
            return forwardError<ShapeGroupLoadResult>(boneR);
        rec.boneId_ = boneR.value_;

        auto offsetR = IRMath::BinaryIO::readVec3(r);
        if (!offsetR.ok())
            return forwardError<ShapeGroupLoadResult>(offsetR);
        rec.offset_ = offsetR.value_;

        auto rotR = IRMath::BinaryIO::readVec4(r);
        if (!rotR.ok())
            return forwardError<ShapeGroupLoadResult>(rotR);
        rec.rotation_ = rotR.value_;

        auto csgR = r.readU8();
        if (!csgR.ok())
            return forwardError<ShapeGroupLoadResult>(csgR);
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
        return forwardError<BoundsPair>(minX);
    auto minY = r.readI32();
    if (!minY.ok())
        return forwardError<BoundsPair>(minY);
    auto minZ = r.readI32();
    if (!minZ.ok())
        return forwardError<BoundsPair>(minZ);
    auto maxX = r.readI32();
    if (!maxX.ok())
        return forwardError<BoundsPair>(maxX);
    auto maxY = r.readI32();
    if (!maxY.ok())
        return forwardError<BoundsPair>(maxY);
    auto maxZ = r.readI32();
    if (!maxZ.ok())
        return forwardError<BoundsPair>(maxZ);
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
        writeVoxelRecordBody(w, v);
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
    if (!verR.ok())
        return forwardError<VoxelRecordsLoadResult>(verR);
    // v1 → v2 migration: pad0_=0 at byte 7 becomes layer_id_=0 (default
    // layer). Wire bytes are identical; no data translation needed.
    // Future field additions bump the version and add a migration case here.
    if (verR.value_ > kVoxelRecordVersion) {
        IRE_LOG_WARN(
            "readVoxelRecordsChunk: record version {} > known version {}; "
            "unknown fields ignored",
            verR.value_,
            static_cast<std::uint16_t>(kVoxelRecordVersion)
        );
    }
    auto countR = r.readVarUInt();
    if (!countR.ok())
        return forwardError<VoxelRecordsLoadResult>(countR);
    if (countR.value_ != static_cast<std::uint64_t>(expectedCount)) {
        IRE_LOG_WARN(
            "readVoxelRecordsChunk: chunk count={} != bounds-derived count={}",
            countR.value_,
            expectedCount
        );
    }
    VoxelRecordsLoadResult out;
    out.recordVersion_ = verR.value_;
    out.voxels_.reserve(cappedReserve(countR.value_, r.remaining() / kRecordBytes));
    for (std::uint64_t i = 0; i < countR.value_; ++i) {
        auto vR = readVoxelRecordBody(r);
        if (!vR.ok())
            return forwardError<VoxelRecordsLoadResult>(vR);
        out.voxels_.push_back(std::move(vR.value_));
    }
    return Result<VoxelRecordsLoadResult>::success(std::move(out));
}

// ---- VRLE chunk -------------------------------------------------------

ChunkPayload makeVoxelRecordsRleChunk(std::span<const VoxelRecord> voxels) {
    // Build RLE triples (emptyRun, filledStart, filledCount) over voxels.
    // A slot is "empty" when alpha == 0. Filled slots are written inline.
    // Trailing empty slots need no triple.
    struct Triple {
        std::uint64_t emptyBefore_;
        const VoxelRecord *filledStart_;
        std::uint64_t filledCount_;
    };
    std::vector<Triple> triples;

    std::uint64_t pendingEmpty = 0;
    bool inFilled = false;
    for (std::size_t i = 0; i < voxels.size(); ++i) {
        const auto &v = voxels[i];
        if (v.color_.alpha_ == 0) {
            if (inFilled) {
                inFilled = false;
                pendingEmpty = 1; // pendingEmpty is always 0 here; cleared on triple push
            } else {
                ++pendingEmpty;
            }
        } else {
            if (!inFilled) {
                triples.push_back({pendingEmpty, &v, 0});
                pendingEmpty = 0;
                inFilled = true;
            }
            ++triples.back().filledCount_;
        }
    }

    MemoryBinaryWriter w;
    w.writeU16(VoxelRecord::kSaveVersion);
    w.writeVarUInt(static_cast<std::uint64_t>(triples.size()));
    for (const auto &t : triples) {
        w.writeVarUInt(t.emptyBefore_);
        w.writeVarUInt(t.filledCount_);
        for (std::uint64_t j = 0; j < t.filledCount_; ++j) {
            writeVoxelRecordBody(w, t.filledStart_[j]);
        }
    }
    ChunkPayload out;
    out.tag_ = kChunkTagVoxelRecordsRle;
    out.data_ = w.takeBuffer();
    return out;
}

Result<VoxelRecordsLoadResult>
readVoxelRecordsRleChunk(std::span<const std::uint8_t> body, std::size_t expectedCount) {
    MemoryBinaryReader r(body.data(), body.size(), "<VRLE chunk>");
    auto verR = r.readU16();
    if (!verR.ok())
        return forwardError<VoxelRecordsLoadResult>(verR);
    if (verR.value_ > kVoxelRecordVersion) {
        IRE_LOG_WARN(
            "readVoxelRecordsRleChunk: record version {} > known version {}; "
            "unknown fields ignored",
            verR.value_,
            static_cast<std::uint16_t>(kVoxelRecordVersion)
        );
    }
    auto tripleCountR = r.readVarUInt();
    if (!tripleCountR.ok())
        return forwardError<VoxelRecordsLoadResult>(tripleCountR);

    VoxelRecordsLoadResult out;
    out.recordVersion_ = verR.value_;
    // Pre-fill with default (all-zero) VoxelRecord; slots not covered by any
    // triple decode as empty (alpha==0).
    out.voxels_.assign(expectedCount, VoxelRecord{});

    std::size_t cursor = 0;
    for (std::uint64_t t = 0; t < tripleCountR.value_; ++t) {
        auto emptyR = r.readVarUInt();
        if (!emptyR.ok())
            return forwardError<VoxelRecordsLoadResult>(emptyR);
        auto filledR = r.readVarUInt();
        if (!filledR.ok())
            return forwardError<VoxelRecordsLoadResult>(filledR);
        cursor += static_cast<std::size_t>(emptyR.value_);
        const std::uint64_t filled = filledR.value_;
        for (std::uint64_t j = 0; j < filled; ++j) {
            auto vR = readVoxelRecordBody(r);
            if (!vR.ok())
                return forwardError<VoxelRecordsLoadResult>(vR);
            if (cursor < out.voxels_.size()) {
                out.voxels_[cursor] = std::move(vR.value_);
            }
            ++cursor;
        }
    }
    // Trailing empty slots at the end of the volume are implicit (no triple
    // needed). Only warn when the RLE data overshot the expected count, which
    // indicates a malformed chunk (not just a sparse/hollow voxel set).
    if (cursor > expectedCount) {
        IRE_LOG_WARN(
            "readVoxelRecordsRleChunk: decoded {} voxels, expected {} "
            "(overflow — truncated at expected boundary)",
            cursor,
            expectedCount
        );
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
    if (!countR.ok())
        return forwardError<std::vector<LayerInfo>>(countR);
    std::vector<LayerInfo> out;
    out.reserve(cappedReserve(countR.value_, r.remaining()));
    for (std::uint64_t i = 0; i < countR.value_; ++i) {
        LayerInfo layer;
        auto nameR = r.readString();
        if (!nameR.ok())
            return forwardError<std::vector<LayerInfo>>(nameR);
        layer.name_ = std::move(nameR.value_);
        auto wordCountR = r.readVarUInt();
        if (!wordCountR.ok())
            return forwardError<std::vector<LayerInfo>>(wordCountR);
        // Cap u64 word count by remaining bytes / 8 to defuse a corrupted
        // count that claims billions of words.
        layer.bitmask_.reserve(cappedReserve(wordCountR.value_, r.remaining() / 8));
        for (std::uint64_t w_i = 0; w_i < wordCountR.value_; ++w_i) {
            auto wordR = r.readU64();
            if (!wordR.ok())
                return forwardError<std::vector<LayerInfo>>(wordR);
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
            IRMath::BinaryIO::writeVec3(w, off);
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
    if (!frameCountR.ok())
        return forwardError<FramesLoadResult>(frameCountR);
    FramesLoadResult out;
    out.frames_.reserve(cappedReserve(frameCountR.value_, r.remaining()));
    for (std::uint64_t i = 0; i < frameCountR.value_; ++i) {
        auto frameIdxR = r.readU32();
        if (!frameIdxR.ok())
            return forwardError<FramesLoadResult>(frameIdxR);
        auto offCountR = r.readVarUInt();
        if (!offCountR.ok())
            return forwardError<FramesLoadResult>(offCountR);
        FramePose frame;
        frame.frameIndex_ = frameIdxR.value_;
        frame.offsets_.reserve(cappedReserve(offCountR.value_, r.remaining() / kVec3Bytes));
        for (std::uint64_t off_i = 0; off_i < offCountR.value_; ++off_i) {
            auto vR = IRMath::BinaryIO::readVec3(r);
            if (!vR.ok())
                return forwardError<FramesLoadResult>(vR);
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
    if (!countR.ok())
        return forwardError<std::vector<MetaEntry>>(countR);
    std::vector<MetaEntry> out;
    out.reserve(cappedReserve(countR.value_, r.remaining()));
    for (std::uint64_t i = 0; i < countR.value_; ++i) {
        MetaEntry entry;
        auto keyR = r.readString();
        if (!keyR.ok())
            return forwardError<std::vector<MetaEntry>>(keyR);
        entry.key_ = std::move(keyR.value_);
        auto valR = r.readString();
        if (!valR.ok())
            return forwardError<std::vector<MetaEntry>>(valR);
        entry.value_ = std::move(valR.value_);
        out.push_back(std::move(entry));
    }
    return Result<std::vector<MetaEntry>>::success(std::move(out));
}

namespace {

// Append the DENSE-mode chunks every full-dense save shares: BNDS, VOXR,
// VRLE (always emitted alongside VOXR — Rule #1 forward compat: old loaders
// skip VRLE and use VOXR; new loaders prefer VRLE). LAYR / FRAM / META are
// additive — missing chunks degrade to empty on load.
void appendDenseChunks(std::vector<ChunkPayload> &chunks, const DenseVoxelSet &dense) {
    chunks.push_back(makeBoundsChunk(dense.boundsMin_, dense.boundsMax_));
    chunks.push_back(makeVoxelRecordsChunk(dense.voxels_));
    chunks.push_back(makeVoxelRecordsRleChunk(dense.voxels_));
    if (!dense.layers_.empty()) {
        chunks.push_back(makeLayersChunk(dense.layers_));
    }
    if (!dense.frames_.empty()) {
        chunks.push_back(makeFramesChunk(dense.frames_));
    }
    if (!dense.meta_.empty()) {
        chunks.push_back(makeMetaChunk(dense.meta_));
    }
}

// Resolve VRLE → VOXR → none into target.voxels_ + target.recordVersion_.
// Returns an error only when the chosen chunk is malformed; if neither
// chunk is present and expectedCount > 0 the helper logs and returns Ok
// (Rule #5 — unknown is recoverable).
BinaryStatus loadVoxelRecordsIntoDense(
    std::span<const LoadedChunk> chunks,
    std::size_t expectedCount,
    DenseVoxelSet &target,
    std::string_view callerName,
    const std::string &path
) {
    // Prefer VRLE (RLE-encoded) if present; fall back to VOXR (dense).
    // Old loaders skip VRLE (Rule #1) and use VOXR; new loaders prefer VRLE.
    if (const LoadedChunk *vrle = findChunk(chunks, kChunkTagVoxelRecordsRle)) {
        auto vR = readVoxelRecordsRleChunk(vrle->data_, expectedCount);
        if (!vR.ok()) {
            return BinaryStatus::error(vR.status_.code_, std::move(vR.status_.message_));
        }
        target.recordVersion_ = vR.value_.recordVersion_;
        target.voxels_ = std::move(vR.value_.voxels_);
        return BinaryStatus::success();
    }
    if (const LoadedChunk *voxr = findChunk(chunks, kChunkTagVoxelRecords)) {
        auto vR = readVoxelRecordsChunk(voxr->data_, expectedCount);
        if (!vR.ok()) {
            return BinaryStatus::error(vR.status_.code_, std::move(vR.status_.message_));
        }
        target.recordVersion_ = vR.value_.recordVersion_;
        target.voxels_ = std::move(vR.value_.voxels_);
        return BinaryStatus::success();
    }
    if (expectedCount > 0) {
        // BNDS present but neither VOXR nor VRLE → malformed save. Rule #5
        // requires we proceed (caller may still want the bounds), but log
        // loudly so the caller knows `voxels_` is empty.
        IRE_LOG_WARN(
            "{}: BNDS present (voxelCount={}) but no VOXR/VRLE chunk in '{}'; "
            "voxels_ left empty",
            callerName,
            expectedCount,
            path
        );
    }
    return BinaryStatus::success();
}

// Load the optional LAYR / FRAM / META chunks into target. Returns an
// error only if a present chunk fails to parse; absent chunks leave their
// target fields empty (Rule #1 — missing chunks degrade to empty).
BinaryStatus loadOptionalDenseChunks(
    std::span<const LoadedChunk> chunks,
    std::size_t expectedCount,
    DenseVoxelSet &target,
    std::size_t &skippedFrames
) {
    if (const LoadedChunk *layr = findChunk(chunks, kChunkTagLayers)) {
        auto lR = readLayersChunk(layr->data_);
        if (!lR.ok()) {
            return BinaryStatus::error(lR.status_.code_, std::move(lR.status_.message_));
        }
        target.layers_ = std::move(lR.value_);
    }
    if (const LoadedChunk *fram = findChunk(chunks, kChunkTagFrames)) {
        auto fR = readFramesChunk(fram->data_, expectedCount);
        if (!fR.ok()) {
            return BinaryStatus::error(fR.status_.code_, std::move(fR.status_.message_));
        }
        target.frames_ = std::move(fR.value_.frames_);
        skippedFrames = fR.value_.skippedFrames_;
    }
    if (const LoadedChunk *meta = findChunk(chunks, kChunkTagMeta)) {
        auto mR = readMetaChunk(meta->data_);
        if (!mR.ok()) {
            return BinaryStatus::error(mR.status_.code_, std::move(mR.status_.message_));
        }
        target.meta_ = std::move(mR.value_);
    }
    return BinaryStatus::success();
}

} // namespace

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
    const auto status = writeChunked(fw, kVoxelSetMagic, kVoxelSetVersion, chunks);
    if (status.ok()) {
        emitVoxelSetSidecar(path, VoxelSetMode::SHAPES, nullptr, records);
    }
    return status;
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
    if (!chunksR.ok())
        return forwardError<VoxelSetFile>(chunksR);
    VoxelSetFile out;
    out.mode_ = readModeChunk(chunksR.value_);

    NameTable diskShapeTypes;
    if (const LoadedChunk *sref = findChunk(chunksR.value_, kChunkTagShapeRefs)) {
        auto entriesR = readShapeRefsChunk(sref->data_);
        if (!entriesR.ok())
            return forwardError<VoxelSetFile>(entriesR);
        diskShapeTypes = NameTable(std::move(entriesR.value_));
    }

    if (const LoadedChunk *shpg = findChunk(chunksR.value_, kChunkTagShapeGroup)) {
        auto recR = readShapeGroupChunk(shpg->data_, diskShapeTypes);
        if (!recR.ok())
            return forwardError<VoxelSetFile>(recR);
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
    std::vector<ChunkPayload> chunks;
    chunks.reserve(7);
    chunks.push_back(makeModeChunk(VoxelSetMode::DENSE));
    appendDenseChunks(chunks, dense);
    const auto status = writeChunked(fw, kVoxelSetMagic, kVoxelSetVersion, chunks);
    if (status.ok()) {
        emitVoxelSetSidecar(path, VoxelSetMode::DENSE, &dense, {});
    }
    return status;
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
    if (!chunksR.ok())
        return forwardError<DenseVoxelSetFile>(chunksR);
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
    if (!boundsR.ok())
        return forwardError<DenseVoxelSetFile>(boundsR);
    out.dense_.boundsMin_ = boundsR.value_.boundsMin_;
    out.dense_.boundsMax_ = boundsR.value_.boundsMax_;
    const std::size_t expectedCount = out.dense_.voxelCount();

    auto recStat = loadVoxelRecordsIntoDense(
        chunksR.value_,
        expectedCount,
        out.dense_,
        "loadDenseVoxelSet",
        path
    );
    if (!recStat.ok())
        return forwardError<DenseVoxelSetFile>(recStat);

    auto optStat =
        loadOptionalDenseChunks(chunksR.value_, expectedCount, out.dense_, out.skippedFrames_);
    if (!optStat.ok())
        return forwardError<DenseVoxelSetFile>(optStat);

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
    chunks.reserve(9);
    chunks.push_back(makeModeChunk(VoxelSetMode::HYBRID));
    chunks.push_back(makeShapeRefsChunk(shapeTypeEntries));
    chunks.push_back(makeShapeGroupChunk(shapes));
    appendDenseChunks(chunks, dense);
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
    if (!chunksR.ok())
        return forwardError<VoxelSetAllFile>(chunksR);
    VoxelSetAllFile out;
    out.mode_ = readModeChunk(chunksR.value_);

    // Shape half
    NameTable diskShapeTypes;
    if (const LoadedChunk *sref = findChunk(chunksR.value_, kChunkTagShapeRefs)) {
        auto entriesR = readShapeRefsChunk(sref->data_);
        if (!entriesR.ok())
            return forwardError<VoxelSetAllFile>(entriesR);
        diskShapeTypes = NameTable(std::move(entriesR.value_));
    }
    if (const LoadedChunk *shpg = findChunk(chunksR.value_, kChunkTagShapeGroup)) {
        auto recR = readShapeGroupChunk(shpg->data_, diskShapeTypes);
        if (!recR.ok())
            return forwardError<VoxelSetAllFile>(recR);
        out.shapeRecords_ = std::move(recR.value_.records_);
        out.unknownShapesSkipped_ = recR.value_.unknownShapesSkipped_;
    }

    // Dense half — absent BNDS means no dense data.
    const LoadedChunk *bndsChunk = findChunk(chunksR.value_, kChunkTagBounds);
    if (bndsChunk == nullptr) {
        return Result<VoxelSetAllFile>::success(std::move(out));
    }
    auto boundsR = readBoundsChunk(bndsChunk->data_);
    if (!boundsR.ok())
        return forwardError<VoxelSetAllFile>(boundsR);
    out.dense_.boundsMin_ = boundsR.value_.boundsMin_;
    out.dense_.boundsMax_ = boundsR.value_.boundsMax_;
    const std::size_t expectedCount = out.dense_.voxelCount();

    auto recStat =
        loadVoxelRecordsIntoDense(chunksR.value_, expectedCount, out.dense_, "loadVoxelSet", path);
    if (!recStat.ok())
        return forwardError<VoxelSetAllFile>(recStat);

    auto optStat =
        loadOptionalDenseChunks(chunksR.value_, expectedCount, out.dense_, out.skippedFrames_);
    if (!optStat.ok())
        return forwardError<VoxelSetAllFile>(optStat);

    return Result<VoxelSetAllFile>::success(std::move(out));
}

} // namespace IRAsset
