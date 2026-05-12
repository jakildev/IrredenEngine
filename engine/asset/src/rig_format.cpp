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

const std::array<char, 4> kJntsTag{'J', 'N', 'T', 'S'};

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
    rig.joints_.reserve(static_cast<std::size_t>(countR.value_));
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
    j.endObject();
    return j.str();
}

} // namespace

BinaryStatus writeRig(BinaryWriter &w, const Rig &rig) {
    MemoryBinaryWriter body;
    if (auto st = encodeJntsChunk(body, rig); !st.ok()) {
        return st;
    }
    std::vector<ChunkPayload> chunks;
    chunks.push_back(ChunkPayload{kJntsTag, body.takeBuffer()});
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
    return decodeJntsChunk(*jnts, r.sourceName());
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
