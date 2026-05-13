#include <gtest/gtest.h>

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/chunk_header.hpp>
#include <irreden/asset/rig_format.hpp>
#include <irreden/voxel/components/component_joint_hierarchy.hpp>
#include <irreden/voxel/rig_bridge.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace IRAsset;

const std::string kTmpDir = "/tmp";

Rig makeSnakeRig(std::size_t numJoints) {
    Rig rig;
    rig.joints_.reserve(numJoints);
    for (std::size_t i = 0; i < numJoints; ++i) {
        RigJoint j;
        // Distinct deterministic values per joint so a stale read surfaces.
        const float f = static_cast<float>(i);
        j.rotation_ = IRMath::vec4(0.0f, 0.0f, f * 0.01f, 1.0f - f * 0.001f);
        j.translation_ = IRMath::vec4(f * 0.5f, 1.0f + f, -2.0f, 0.0f);
        j.parentIndex_ = (i == 0) ? 0u : static_cast<std::uint32_t>(i - 1);
        j.name_ = "joint_" + std::to_string(i);
        rig.joints_.push_back(j);
    }
    return rig;
}

void expectRigsEqual(const Rig &a, const Rig &b) {
    ASSERT_EQ(a.joints_.size(), b.joints_.size());
    for (std::size_t i = 0; i < a.joints_.size(); ++i) {
        const auto &ja = a.joints_[i];
        const auto &jb = b.joints_[i];
        EXPECT_EQ(ja.rotation_.x, jb.rotation_.x) << "joint " << i;
        EXPECT_EQ(ja.rotation_.y, jb.rotation_.y) << "joint " << i;
        EXPECT_EQ(ja.rotation_.z, jb.rotation_.z) << "joint " << i;
        EXPECT_EQ(ja.rotation_.w, jb.rotation_.w) << "joint " << i;
        EXPECT_EQ(ja.translation_.x, jb.translation_.x) << "joint " << i;
        EXPECT_EQ(ja.translation_.y, jb.translation_.y) << "joint " << i;
        EXPECT_EQ(ja.translation_.z, jb.translation_.z) << "joint " << i;
        EXPECT_EQ(ja.translation_.w, jb.translation_.w) << "joint " << i;
        EXPECT_EQ(ja.parentIndex_, jb.parentIndex_) << "joint " << i;
        EXPECT_EQ(ja.name_, jb.name_) << "joint " << i;
    }
}

std::string readFileBytes(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ---- Buffer round-trips --------------------------------------------------

TEST(RigFormat, EmptyRigRoundTrip) {
    MemoryBinaryWriter w;
    ASSERT_TRUE(writeRig(w, Rig{}).ok());

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readRig(r);
    ASSERT_TRUE(loaded.ok()) << loaded.status_.message_;
    EXPECT_TRUE(loaded.value_.joints_.empty());
}

TEST(RigFormat, ThirtyBoneSnakeRoundTrip) {
    const Rig original = makeSnakeRig(30);

    MemoryBinaryWriter w;
    ASSERT_TRUE(writeRig(w, original).ok());

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readRig(r);
    ASSERT_TRUE(loaded.ok()) << loaded.status_.message_;
    expectRigsEqual(original, loaded.value_);
}

TEST(RigFormat, AnonymousJointsRoundTrip) {
    Rig original;
    for (int i = 0; i < 4; ++i) {
        RigJoint j;
        j.translation_ = IRMath::vec4(static_cast<float>(i), 0.0f, 0.0f, 0.0f);
        j.parentIndex_ = i == 0 ? 0 : static_cast<std::uint32_t>(i - 1);
        original.joints_.push_back(j);
    }

    MemoryBinaryWriter w;
    ASSERT_TRUE(writeRig(w, original).ok());

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readRig(r);
    ASSERT_TRUE(loaded.ok()) << loaded.status_.message_;
    expectRigsEqual(original, loaded.value_);
}

// ---- Recoverable errors --------------------------------------------------

TEST(RigFormat, BadMagicIsRecoverable) {
    // 12-byte header + a single empty chunk-table entry's worth of nothing
    // is enough to fail BadMagic before reaching any chunk read.
    std::vector<std::uint8_t> bad(64, 0);
    bad[0] = 'X';
    bad[1] = 'X';
    bad[2] = 'X';
    bad[3] = 'X';
    MemoryBinaryReader r(bad.data(), bad.size());
    auto loaded = readRig(r);
    ASSERT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.status_.code_, BinaryIOError::BadMagic);
}

TEST(RigFormat, VersionTooNewIsRecoverable) {
    MemoryBinaryWriter w;
    ASSERT_TRUE(writeRig(w, makeSnakeRig(3)).ok());
    // Patch the version dword (bytes 4..7) to a future value.
    auto buffer = w.takeBuffer();
    buffer[4] = 0xFF;
    buffer[5] = 0xFF;
    buffer[6] = 0xFF;
    buffer[7] = 0xFF;
    MemoryBinaryReader r(buffer.data(), buffer.size());
    auto loaded = readRig(r);
    ASSERT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.status_.code_, BinaryIOError::VersionTooNew);
}

TEST(RigFormat, MissingFileIsRecoverable) {
    auto loaded = loadRig("definitely_not_a_real_rig_filename", kTmpDir);
    ASSERT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.status_.code_, BinaryIOError::OpenFailed);
    EXPECT_TRUE(loaded.value_.joints_.empty());
}

// ---- Forward-compat: unknown chunks survive a load -----------------------

TEST(RigFormat, UnknownChunksAreSilentlySkipped) {
    // Hand-roll a file with JNTS + two truly-unknown future chunks ("ATTC"
    // for hypothetical attachment data, "MORE" for anything else). A v1
    // loader should pull JNTS through and silently ignore the unknowns —
    // Save Format Extensibility Rule #1.
    const Rig original = makeSnakeRig(5);
    MemoryBinaryWriter jntsBody;
    jntsBody.writeVarUInt(original.joints_.size());
    for (const auto &j : original.joints_) {
        jntsBody.writeU16(kJointRecordVersion);
        jntsBody.writeF32(j.rotation_.x);
        jntsBody.writeF32(j.rotation_.y);
        jntsBody.writeF32(j.rotation_.z);
        jntsBody.writeF32(j.rotation_.w);
        jntsBody.writeF32(j.translation_.x);
        jntsBody.writeF32(j.translation_.y);
        jntsBody.writeF32(j.translation_.z);
        jntsBody.writeF32(j.translation_.w);
        jntsBody.writeU32(j.parentIndex_);
        jntsBody.writeString(j.name_);
    }

    std::vector<ChunkPayload> chunks;
    chunks.push_back(ChunkPayload{makeTag("ATTC"), {0xCA, 0xFE, 0xBA, 0xBE}});
    chunks.push_back(ChunkPayload{makeTag("JNTS"), jntsBody.takeBuffer()});
    chunks.push_back(ChunkPayload{makeTag("MORE"), {0x01, 0x02, 0x03}});

    MemoryBinaryWriter w;
    ASSERT_TRUE(writeChunked(w, kRigMagic, kRigFormatVersion, chunks).ok());

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readRig(r);
    ASSERT_TRUE(loaded.ok()) << loaded.status_.message_;
    expectRigsEqual(original, loaded.value_);
}

TEST(RigFormat, NoJntsChunkReturnsEmptyRig) {
    // A file with only unknown chunks and no JNTS should still load
    // (as an empty rig) rather than erroring — a future writer might emit
    // ANIM without JNTS for a pose-only override.
    std::vector<ChunkPayload> chunks;
    chunks.push_back(ChunkPayload{makeTag("ANIM"), {0xCA, 0xFE, 0xBA, 0xBE}});

    MemoryBinaryWriter w;
    ASSERT_TRUE(writeChunked(w, kRigMagic, kRigFormatVersion, chunks).ok());

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readRig(r);
    ASSERT_TRUE(loaded.ok()) << loaded.status_.message_;
    EXPECT_TRUE(loaded.value_.joints_.empty());
}

// ---- File-mode round-trip ------------------------------------------------

TEST(RigFormat, FileRoundTripAndSidecarEmitted) {
    const std::string name = "snake_test";
    const Rig original = makeSnakeRig(30);

    // Cleanup any prior fixture.
    std::remove((kTmpDir + "/" + name + ".rig").c_str());
    std::remove((kTmpDir + "/" + name + ".rig.json").c_str());

    ASSERT_TRUE(saveRig(name, kTmpDir, original).ok());

    auto loaded = loadRig(name, kTmpDir);
    ASSERT_TRUE(loaded.ok()) << loaded.status_.message_;
    expectRigsEqual(original, loaded.value_);

    // Sidecar should be present and contain per-joint names + parent
    // indices for a few joints we can recognize.
    const std::string sidecar = readFileBytes(kTmpDir + "/" + name + ".rig.json");
    ASSERT_FALSE(sidecar.empty());
    EXPECT_NE(sidecar.find("\"format\""), std::string::npos);
    EXPECT_NE(sidecar.find("\"rig\""), std::string::npos);
    EXPECT_NE(sidecar.find("\"jointCount\""), std::string::npos);
    EXPECT_NE(sidecar.find("\"joint_0\""), std::string::npos);
    EXPECT_NE(sidecar.find("\"joint_29\""), std::string::npos);
    EXPECT_NE(sidecar.find("\"parentIndex\""), std::string::npos);
}

// ---- Bridge: round-trip preserves C_JointHierarchy::toGPUFormat() --------

TEST(RigFormat, GPUMatrixParityAfterRoundTrip) {
    // The fundamental acceptance criterion from #666: a save → load
    // round-trip must produce the same GPU upload payload, byte for byte,
    // so the shapes compute shader reads identical joint transforms.
    IRComponents::C_JointHierarchy original;
    for (int i = 0; i < 30; ++i) {
        IRComponents::Joint j;
        const float f = static_cast<float>(i);
        j.rotation_ = IRMath::vec4(0.0f, 0.0f, f * 0.01f, 1.0f - f * 0.001f);
        j.translation_ = IRMath::vec4(f * 0.5f, 1.0f + f, -2.0f, 0.0f);
        j.parentIndex_ = (i == 0) ? 0u : static_cast<std::uint32_t>(i - 1);
        original.joints_.push_back(j);
    }

    std::vector<std::string> names;
    names.reserve(30);
    for (int i = 0; i < 30; ++i) {
        names.push_back("joint_" + std::to_string(i));
    }
    const IRAsset::Rig rig =
        IRPrefab::Rig::fromComponent(original, std::span<const std::string>(names));

    MemoryBinaryWriter w;
    ASSERT_TRUE(writeRig(w, rig).ok());

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loadedRig = readRig(r);
    ASSERT_TRUE(loadedRig.ok());

    const IRComponents::C_JointHierarchy roundTripped =
        IRPrefab::Rig::toComponent(loadedRig.value_);

    const auto gpuBefore = original.toGPUFormat();
    const auto gpuAfter = roundTripped.toGPUFormat();
    ASSERT_EQ(gpuBefore.size(), gpuAfter.size());
    EXPECT_EQ(
        0,
        std::memcmp(
            gpuBefore.data(),
            gpuAfter.data(),
            gpuBefore.size() * sizeof(IRRender::GPUJointTransform)
        )
    );
}

TEST(RigFormat, BridgeAnonymousJointsPreserveTransforms) {
    // No names supplied — bridge should leave name_ empty on every joint,
    // and the round-trip transforms still match.
    IRComponents::C_JointHierarchy hierarchy;
    for (int i = 0; i < 5; ++i) {
        IRComponents::Joint j;
        j.translation_ = IRMath::vec4(static_cast<float>(i), 2.0f, 3.0f, 0.0f);
        j.parentIndex_ = i == 0 ? 0u : static_cast<std::uint32_t>(i - 1);
        hierarchy.joints_.push_back(j);
    }
    const IRAsset::Rig rig = IRPrefab::Rig::fromComponent(hierarchy);
    for (const auto &j : rig.joints_) {
        EXPECT_TRUE(j.name_.empty());
    }
    const auto restored = IRPrefab::Rig::toComponent(rig);
    ASSERT_EQ(restored.joints_.size(), hierarchy.joints_.size());
    for (std::size_t i = 0; i < restored.joints_.size(); ++i) {
        EXPECT_EQ(restored.joints_[i].parentIndex_, hierarchy.joints_[i].parentIndex_);
        EXPECT_EQ(restored.joints_[i].translation_.x, hierarchy.joints_[i].translation_.x);
    }
}

// ---- BIND chunk: round-trips and error cases --------------------------------

TEST(RigFormat, BindPointRoundTrip) {
    Rig original = makeSnakeRig(5);
    for (int i = 0; i < 3; ++i) {
        RigBindPoint bp;
        bp.boneId_ = static_cast<std::uint32_t>(i + 1);
        bp.offset_ = IRMath::vec3(static_cast<float>(i), 0.5f, -1.0f);
        bp.rotation_ = IRMath::vec4(0.0f, 0.0f, static_cast<float>(i) * 0.1f, 1.0f);
        bp.name_ = "bind_" + std::to_string(i);
        original.bindPoints_.push_back(bp);
    }

    MemoryBinaryWriter w;
    ASSERT_TRUE(writeRig(w, original).ok());

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readRig(r);
    ASSERT_TRUE(loaded.ok()) << loaded.status_.message_;

    ASSERT_EQ(loaded.value_.joints_.size(), original.joints_.size());
    ASSERT_EQ(loaded.value_.bindPoints_.size(), original.bindPoints_.size());
    for (std::size_t i = 0; i < original.bindPoints_.size(); ++i) {
        const auto &a = original.bindPoints_[i];
        const auto &b = loaded.value_.bindPoints_[i];
        EXPECT_EQ(b.boneId_, a.boneId_) << "bind point " << i;
        EXPECT_EQ(b.offset_.x, a.offset_.x) << "bind point " << i;
        EXPECT_EQ(b.offset_.y, a.offset_.y) << "bind point " << i;
        EXPECT_EQ(b.offset_.z, a.offset_.z) << "bind point " << i;
        EXPECT_EQ(b.rotation_.x, a.rotation_.x) << "bind point " << i;
        EXPECT_EQ(b.rotation_.y, a.rotation_.y) << "bind point " << i;
        EXPECT_EQ(b.rotation_.z, a.rotation_.z) << "bind point " << i;
        EXPECT_EQ(b.rotation_.w, a.rotation_.w) << "bind point " << i;
        EXPECT_EQ(b.name_, a.name_) << "bind point " << i;
    }
}

TEST(RigFormat, EmptyBindPointsRoundTrip) {
    // A rig with no bind points must load with bindPoints_ empty (not
    // populated with garbage) and must not emit a BIND chunk.
    const Rig original = makeSnakeRig(4);
    ASSERT_TRUE(original.bindPoints_.empty());

    MemoryBinaryWriter w;
    ASSERT_TRUE(writeRig(w, original).ok());

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readRig(r);
    ASSERT_TRUE(loaded.ok());
    EXPECT_TRUE(loaded.value_.bindPoints_.empty());
}

TEST(RigFormat, BindPointVersionTooNewIsRecoverable) {
    // Hand-craft a BIND chunk whose per-record version is a future value.
    // The loader must surface VersionTooNew, not crash.
    const Rig original = makeSnakeRig(2);
    MemoryBinaryWriter jntsBody;
    jntsBody.writeVarUInt(original.joints_.size());
    for (const auto &j : original.joints_) {
        jntsBody.writeU16(kJointRecordVersion);
        jntsBody.writeF32(j.rotation_.x);
        jntsBody.writeF32(j.rotation_.y);
        jntsBody.writeF32(j.rotation_.z);
        jntsBody.writeF32(j.rotation_.w);
        jntsBody.writeF32(j.translation_.x);
        jntsBody.writeF32(j.translation_.y);
        jntsBody.writeF32(j.translation_.z);
        jntsBody.writeF32(j.translation_.w);
        jntsBody.writeU32(j.parentIndex_);
        jntsBody.writeString(j.name_);
    }

    // One bind point with a record version far above kBindPointRecordVersion.
    MemoryBinaryWriter bindBody;
    bindBody.writeVarUInt(1u);
    bindBody.writeU16(0xFFFF); // future record version
    bindBody.writeU32(0u);     // boneId
    bindBody.writeF32(0.0f);   // offset x
    bindBody.writeF32(0.0f);   // offset y
    bindBody.writeF32(0.0f);   // offset z
    bindBody.writeF32(0.0f);   // rotation x
    bindBody.writeF32(0.0f);   // rotation y
    bindBody.writeF32(0.0f);   // rotation z
    bindBody.writeF32(1.0f);   // rotation w
    bindBody.writeString("");  // name

    std::vector<ChunkPayload> chunks;
    chunks.push_back(ChunkPayload{makeTag("JNTS"), jntsBody.takeBuffer()});
    chunks.push_back(ChunkPayload{makeTag("BIND"), bindBody.takeBuffer()});

    MemoryBinaryWriter w;
    ASSERT_TRUE(writeChunked(w, kRigMagic, kRigFormatVersion, chunks).ok());

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readRig(r);
    ASSERT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.status_.code_, BinaryIOError::VersionTooNew);
}

TEST(RigFormat, BindChunkSidecarContent) {
    const std::string name = "bind_sidecar_test";
    Rig original = makeSnakeRig(3);
    {
        RigBindPoint bp;
        bp.boneId_ = 2;
        bp.offset_ = IRMath::vec3(0.5f, 1.0f, -0.25f);
        bp.rotation_ = IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        bp.name_ = "sword_grip";
        original.bindPoints_.push_back(bp);
    }

    std::remove((kTmpDir + "/" + name + ".rig").c_str());
    std::remove((kTmpDir + "/" + name + ".rig.json").c_str());

    ASSERT_TRUE(saveRig(name, kTmpDir, original).ok());

    auto loaded = loadRig(name, kTmpDir);
    ASSERT_TRUE(loaded.ok()) << loaded.status_.message_;
    ASSERT_EQ(loaded.value_.bindPoints_.size(), 1u);
    EXPECT_EQ(loaded.value_.bindPoints_[0].name_, "sword_grip");
    EXPECT_EQ(loaded.value_.bindPoints_[0].boneId_, 2u);

    const std::string sidecar = readFileBytes(kTmpDir + "/" + name + ".rig.json");
    ASSERT_FALSE(sidecar.empty());
    EXPECT_NE(sidecar.find("\"bindPointCount\""), std::string::npos);
    EXPECT_NE(sidecar.find("\"bindPoints\""), std::string::npos);
    EXPECT_NE(sidecar.find("\"sword_grip\""), std::string::npos);
    EXPECT_NE(sidecar.find("\"boneId\""), std::string::npos);
}

} // namespace
