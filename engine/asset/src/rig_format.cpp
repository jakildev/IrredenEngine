#include <irreden/asset/rig_format.hpp>

#include <irreden/asset/chunk_header.hpp>
#include <irreden/asset/json_sidecar.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_utility.hpp>

#include <utility>

namespace IRAsset {

namespace {

constexpr const char *kRigExtension = ".rig";
constexpr const char *kRigSidecarExtension = ".rig.json";

constexpr std::array<char, 4> kJntsTag{'J', 'N', 'T', 'S'};
constexpr std::array<char, 4> kBindTag{'B', 'I', 'N', 'D'};

void encodeVec4(BinaryWriter &w, const IRMath::vec4 &v) {
    w.writeF32(v.x);
    w.writeF32(v.y);
    w.writeF32(v.z);
    w.writeF32(v.w);
}

Result<IRMath::vec4> decodeVec4(BinaryReader &r) {
    auto x = r.readF32();
    if (!x.ok())
        return Result<IRMath::vec4>::error(x.status_.code_, std::move(x.status_.message_));
    auto y = r.readF32();
    if (!y.ok())
        return Result<IRMath::vec4>::error(y.status_.code_, std::move(y.status_.message_));
    auto z = r.readF32();
    if (!z.ok())
        return Result<IRMath::vec4>::error(z.status_.code_, std::move(z.status_.message_));
    auto w = r.readF32();
    if (!w.ok())
        return Result<IRMath::vec4>::error(w.status_.code_, std::move(w.status_.message_));
    return Result<IRMath::vec4>::success(IRMath::vec4(x.value_, y.value_, z.value_, w.value_));
}

BinaryStatus encodeJntsChunk(MemoryBinaryWriter &body, const Rig &rig) {
    body.writeVarUInt(rig.joints_.size());
    for (const auto &j : rig.joints_) {
        body.writeU16(kJointRecordVersion);
        encodeVec4(body, j.rotation_);
        encodeVec4(body, j.translation_);
        body.writeU32(j.parentIndex_);
        body.writeString(j.name_);
    }
    if (body.failed()) {
        return BinaryStatus::error(BinaryIOError::WriteFailed, body.failureMessage());
    }
    return BinaryStatus::success();
}

Result<Rig> decodeJntsChunk(const LoadedChunk &chunk, const std::string &sourceName) {
    MemoryBinaryReader r(chunk.data_.data(), chunk.data_.size(), sourceName + ":JNTS");
    Rig rig;
    auto countR = r.readVarUInt();
    if (!countR.ok()) {
        return Result<Rig>::error(countR.status_.code_, std::move(countR.status_.message_));
    }
    // Cap the upfront reserve to the remaining bytes so a corrupted
    // jointCount claiming billions of joints can't pre-allocate.
    const std::uint64_t cap = r.remaining();
    rig.joints_.reserve(static_cast<std::size_t>(countR.value_ < cap ? countR.value_ : cap));
    for (std::uint64_t i = 0; i < countR.value_; ++i) {
        auto verR = r.readU16();
        if (!verR.ok()) {
            return Result<Rig>::error(verR.status_.code_, std::move(verR.status_.message_));
        }
        if (verR.value_ > kJointRecordVersion) {
            return Result<Rig>::error(
                BinaryIOError::VersionTooNew,
                "joint record version " + std::to_string(verR.value_) + " above max known " +
                    std::to_string(kJointRecordVersion) + " in " + r.sourceName()
            );
        }
        RigJoint joint;
        auto rot = decodeVec4(r);
        if (!rot.ok())
            return Result<Rig>::error(rot.status_.code_, std::move(rot.status_.message_));
        joint.rotation_ = rot.value_;
        auto tra = decodeVec4(r);
        if (!tra.ok())
            return Result<Rig>::error(tra.status_.code_, std::move(tra.status_.message_));
        joint.translation_ = tra.value_;
        auto parent = r.readU32();
        if (!parent.ok()) {
            return Result<Rig>::error(parent.status_.code_, std::move(parent.status_.message_));
        }
        joint.parentIndex_ = parent.value_;
        auto nm = r.readString();
        if (!nm.ok())
            return Result<Rig>::error(nm.status_.code_, std::move(nm.status_.message_));
        joint.name_ = std::move(nm.value_);
        rig.joints_.push_back(std::move(joint));
    }
    return Result<Rig>::success(std::move(rig));
}

BinaryStatus encodeBindChunk(MemoryBinaryWriter &body, const Rig &rig) {
    body.writeVarUInt(rig.bindPoints_.size());
    for (const auto &bp : rig.bindPoints_) {
        body.writeU16(kBindPointRecordVersion);
        body.writeU32(bp.boneId_);
        body.writeF32(bp.offset_.x);
        body.writeF32(bp.offset_.y);
        body.writeF32(bp.offset_.z);
        body.writeF32(bp.rotation_.x);
        body.writeF32(bp.rotation_.y);
        body.writeF32(bp.rotation_.z);
        body.writeF32(bp.rotation_.w);
        body.writeString(bp.name_);
    }
    if (body.failed()) {
        return BinaryStatus::error(BinaryIOError::WriteFailed, body.failureMessage());
    }
    return BinaryStatus::success();
}

BinaryStatus decodeBindChunk(const LoadedChunk &chunk, const std::string &sourceName, Rig &outRig) {
    MemoryBinaryReader r(chunk.data_.data(), chunk.data_.size(), sourceName + ":BIND");
    auto countR = r.readVarUInt();
    if (!countR.ok()) {
        return BinaryStatus::error(countR.status_.code_, std::move(countR.status_.message_));
    }
    const std::uint64_t cap = r.remaining();
    outRig.bindPoints_.reserve(static_cast<std::size_t>(countR.value_ < cap ? countR.value_ : cap));
    for (std::uint64_t i = 0; i < countR.value_; ++i) {
        auto verR = r.readU16();
        if (!verR.ok()) {
            return BinaryStatus::error(verR.status_.code_, std::move(verR.status_.message_));
        }
        if (verR.value_ > kBindPointRecordVersion) {
            return BinaryStatus::error(
                BinaryIOError::VersionTooNew,
                "bind-point record version " + std::to_string(verR.value_) + " above max known " +
                    std::to_string(kBindPointRecordVersion) + " in " + r.sourceName()
            );
        }
        RigBindPoint bp;
        auto boneR = r.readU32();
        if (!boneR.ok()) {
            return BinaryStatus::error(boneR.status_.code_, std::move(boneR.status_.message_));
        }
        bp.boneId_ = boneR.value_;
        auto ox = r.readF32();
        if (!ox.ok())
            return BinaryStatus::error(ox.status_.code_, std::move(ox.status_.message_));
        auto oy = r.readF32();
        if (!oy.ok())
            return BinaryStatus::error(oy.status_.code_, std::move(oy.status_.message_));
        auto oz = r.readF32();
        if (!oz.ok())
            return BinaryStatus::error(oz.status_.code_, std::move(oz.status_.message_));
        bp.offset_ = IRMath::vec3(ox.value_, oy.value_, oz.value_);
        auto rot = decodeVec4(r);
        if (!rot.ok())
            return BinaryStatus::error(rot.status_.code_, std::move(rot.status_.message_));
        bp.rotation_ = rot.value_;
        auto nm = r.readString();
        if (!nm.ok())
            return BinaryStatus::error(nm.status_.code_, std::move(nm.status_.message_));
        bp.name_ = std::move(nm.value_);
        outRig.bindPoints_.push_back(std::move(bp));
    }
    return BinaryStatus::success();
}

std::string emitSidecarJson(const Rig &rig) {
    JsonSidecarWriter j;
    j.beginObject();
    j.key("format");
    j.valueString("rig");
    j.key("version");
    j.valueUInt(kRigFormatVersion);
    j.key("jointCount");
    j.valueUInt(static_cast<std::uint64_t>(rig.joints_.size()));
    j.key("joints");
    j.beginArray();
    for (std::size_t i = 0; i < rig.joints_.size(); ++i) {
        const auto &joint = rig.joints_[i];
        j.beginObject();
        j.key("index");
        j.valueUInt(static_cast<std::uint64_t>(i));
        j.key("name");
        j.valueString(joint.name_);
        j.key("parentIndex");
        j.valueUInt(joint.parentIndex_);
        j.endObject();
    }
    j.endArray();
    j.key("bindPointCount");
    j.valueUInt(static_cast<std::uint64_t>(rig.bindPoints_.size()));
    j.key("bindPoints");
    j.beginArray();
    for (std::size_t i = 0; i < rig.bindPoints_.size(); ++i) {
        const auto &bp = rig.bindPoints_[i];
        j.beginObject();
        j.key("index");
        j.valueUInt(static_cast<std::uint64_t>(i));
        j.key("name");
        j.valueString(bp.name_);
        j.key("boneId");
        j.valueUInt(bp.boneId_);
        j.endObject();
    }
    j.endArray();
    j.endObject();
    return j.str();
}

} // namespace

BinaryStatus writeRig(BinaryWriter &w, const Rig &rig) {
    MemoryBinaryWriter jntsBody;
    if (auto st = encodeJntsChunk(jntsBody, rig); !st.ok()) {
        return st;
    }
    std::vector<ChunkPayload> chunks;
    chunks.push_back(ChunkPayload{kJntsTag, jntsBody.takeBuffer()});
    if (!rig.bindPoints_.empty()) {
        MemoryBinaryWriter bindBody;
        if (auto st = encodeBindChunk(bindBody, rig); !st.ok()) {
            return st;
        }
        chunks.push_back(ChunkPayload{kBindTag, bindBody.takeBuffer()});
    }
    return writeChunked(w, kRigMagic, kRigFormatVersion, chunks);
}

Result<Rig> readRig(BinaryReader &r) {
    AssetHeader header{};
    auto chunksR = readChunks(r, kRigMagic, kRigFormatVersion, &header);
    if (!chunksR.ok()) {
        return Result<Rig>::error(chunksR.status_.code_, std::move(chunksR.status_.message_));
    }
    const LoadedChunk *jnts = findChunk(chunksR.value_, kJntsTag);
    if (!jnts) {
        // No joints chunk — return an empty rig. A future writer might emit
        // BIND/ANIM with no JNTS (e.g. a pose-only override) and an older
        // loader should treat that as "no skeleton" rather than corrupt.
        return Result<Rig>::success(Rig{});
    }
    auto rigR = decodeJntsChunk(*jnts, r.sourceName());
    if (!rigR.ok()) {
        return rigR;
    }
    const LoadedChunk *bind = findChunk(chunksR.value_, kBindTag);
    if (bind) {
        if (auto st = decodeBindChunk(*bind, r.sourceName(), rigR.value_); !st.ok()) {
            return Result<Rig>::error(st.code_, std::move(st.message_));
        }
    }
    return rigR;
}

BinaryStatus saveRig(const std::string &name, const std::string &path, const Rig &rig) {
    const std::string filename = IRUtility::joinPath(path, name, kRigExtension);
    FileBinaryWriter w(filename);
    if (!w.ok()) {
        IRE_LOG_ERROR("Failed to open rig file for writing: {}", filename);
        return BinaryStatus::error(BinaryIOError::OpenFailed, "failed to open " + filename);
    }
    if (auto st = writeRig(w, rig); !st.ok()) {
        IRE_LOG_ERROR("Failed to write rig {}: {}", filename, st.message_);
        return st;
    }

    const std::string sidecarPath = IRUtility::joinPath(path, name, kRigSidecarExtension);
    if (!writeJsonSidecarToFile(sidecarPath, emitSidecarJson(rig))) {
        IRE_LOG_WARN("Saved {} but sidecar emit to {} failed", filename, sidecarPath);
    }

    IRE_LOG_INFO("Saved rig with {} joint(s) to {}", rig.joints_.size(), filename);
    return BinaryStatus::success();
}

Result<Rig> loadRig(const std::string &name, const std::string &path) {
    const std::string filename = IRUtility::joinPath(path, name, kRigExtension);
    FileBinaryReader r(filename);
    if (!r.ok()) {
        IRE_LOG_ERROR("Failed to open rig file for reading: {}", filename);
        return Result<Rig>::error(BinaryIOError::OpenFailed, "failed to open " + filename);
    }
    auto rigR = readRig(r);
    if (!rigR.ok()) {
        IRE_LOG_ERROR("Failed to load rig {}: {}", filename, rigR.status_.message_);
    } else {
        IRE_LOG_INFO("Loaded rig with {} joint(s) from {}", rigR.value_.joints_.size(), filename);
    }
    return rigR;
}

} // namespace IRAsset
