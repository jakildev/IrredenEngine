#include "../../creations/editors/voxel_editor/scene_io.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
using namespace IRVoxelEditor;

class VoxelEditorSceneIO : public testing::Test {
  protected:
    void SetUp() override {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_directory =
            std::filesystem::temp_directory_path() / ("ir_scene_io_" + std::to_string(stamp));
        m_owned = std::filesystem::create_directory(m_directory);
        ASSERT_TRUE(m_owned);
        m_animation.frames_.resize(2);
        m_animation.activeFrame_ = 1;
        m_frames = {
            {IRComponents::C_Voxel{IRMath::Color{20, 30, 40, 255}}},
            {IRComponents::C_Voxel{IRMath::Color{60, 70, 80, 255}}}
        };
    }
    void TearDown() override {
        if (m_owned) {
            std::error_code error;
            std::filesystem::remove_all(m_directory, error);
        }
    }
    SaveResult save(const IRAsset::Rig *rig = nullptr) {
        return saveEditorScene(
            m_directory.string(),
            "named",
            m_frames,
            {1, 1, 1},
            m_layers,
            m_animation,
            m_symmetry,
            rig
        );
    }
    LoadResult load() {
        return loadEditorScene(m_directory.string(), "named");
    }
    std::filesystem::path frame(int index) {
        return m_directory / ("named_frame_" + std::to_string(index) + ".vxs");
    }
    static std::string read(const std::filesystem::path &path) {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
    void rewriteMetadata(const std::string &key, const std::string &value) {
        auto loaded = IRAsset::loadDenseVoxelSet(frame(0).string());
        ASSERT_TRUE(loaded.ok());
        for (auto &entry : loaded.value_.dense_.meta_) {
            if (entry.key_ == key) {
                entry.value_ = value;
                ASSERT_TRUE(
                    IRAsset::saveDenseVoxelSet(frame(0).string(), loaded.value_.dense_).ok()
                );
                return;
            }
        }
        FAIL() << "Missing metadata " << key;
    }
    std::filesystem::path m_directory;
    bool m_owned = false;
    AnimationState m_animation;
    EditorLayerManager m_layers;
    SymmetryState m_symmetry;
    std::vector<std::vector<IRComponents::C_Voxel>> m_frames;
};

TEST_F(VoxelEditorSceneIO, NamedAnimationRoundTripKeepsFramesLayersAndSettings) {
    const auto layer = m_layers.addLayer("leaves", IRMath::Color{1, 2, 3, 4});
    m_layers.setActiveLayer(layer);
    m_layers.toggleLayerVisibility(layer);
    m_frames[1][0].layer_id_ = layer;
    m_animation.fps_ = 7.5f;
    m_animation.loopMode_ = LoopMode::PING_PONG;
    m_symmetry.enableX_ = true;
    m_symmetry.offsetX_ = 2.5f;
    ASSERT_TRUE(save().ok_);
    const auto loaded = load();
    ASSERT_TRUE(loaded.ok_) << loaded.errorMsg_;
    ASSERT_EQ(loaded.frameSnapshots_.size(), 2u);
    EXPECT_EQ(loaded.frameSnapshots_[0][0].color_.red_, 20);
    EXPECT_EQ(loaded.frameSnapshots_[1][0].color_.red_, 60);
    EXPECT_EQ(loaded.frameSnapshots_[1][0].layer_id_, layer);
    EXPECT_EQ(loaded.gridSize_, IRMath::ivec3(1));
    EXPECT_EQ(loaded.activeFrame_, 1);
    EXPECT_FLOAT_EQ(loaded.fps_, 7.5f);
    EXPECT_EQ(loaded.loopMode_, LoopMode::PING_PONG);
    EXPECT_TRUE(loaded.symmetry_.enableX_);
    EXPECT_FLOAT_EQ(loaded.symmetry_.offsetX_, 2.5f);
    ASSERT_EQ(loaded.layers_.size(), 2u);
    EXPECT_EQ(loaded.layers_[1].name_, "leaves");
    EXPECT_FALSE(loaded.layers_[1].visible_);
    EXPECT_EQ(loaded.activeLayerId_, layer);
    EXPECT_EQ(loaded.nextLayerId_, 2);
    EXPECT_TRUE(std::filesystem::is_regular_file(frame(0).string() + ".json"));
    EXPECT_FALSE(std::filesystem::exists(m_directory / ".named.saving"));
}

TEST_F(VoxelEditorSceneIO, RigRoundTripIsDeclaredByMetadataAndIgnoresStaleRig) {
    IRAsset::Rig rig;
    rig.joints_.resize(2);
    rig.joints_[0].name_ = "root";
    rig.joints_[1].name_ = "branch";
    rig.joints_[1].parentIndex_ = 0;
    rig.joints_[1].translation_ = {1.0f, 2.0f, 3.0f, 0.0f};
    ASSERT_TRUE(save(&rig).ok_);
    auto loaded = load();
    ASSERT_TRUE(loaded.ok_) << loaded.errorMsg_;
    ASSERT_TRUE(loaded.rig_);
    ASSERT_EQ(loaded.rig_->joints_.size(), 2u);
    EXPECT_EQ(loaded.rig_->joints_[1].name_, "branch");
    EXPECT_EQ(loaded.rig_->joints_[1].translation_, rig.joints_[1].translation_);
    EXPECT_EQ(loaded.rig_->joints_[1].parentIndex_, 0u);
    ASSERT_TRUE(save().ok_);
    ASSERT_TRUE(std::filesystem::is_regular_file(m_directory / "named.rig"));
    loaded = load();
    ASSERT_TRUE(loaded.ok_);
    EXPECT_FALSE(loaded.rig_);
}

TEST_F(VoxelEditorSceneIO, FailedPublicationPreservesRigAndFramesTogether) {
    IRAsset::Rig rig;
    rig.joints_.resize(1);
    ASSERT_TRUE(save(&rig).ok_);
    const auto originalRig = read(m_directory / "named.rig");
    const auto first = read(frame(0));
    const auto second = read(frame(1));
    m_frames.push_back(m_frames[0]);
    m_animation.frames_.resize(3);
    ASSERT_TRUE(std::filesystem::create_directory(frame(2)));
    rig.joints_[0].translation_.x = 5.0f;
    EXPECT_FALSE(save(&rig).ok_);
    EXPECT_EQ(read(m_directory / "named.rig"), originalRig);
    EXPECT_EQ(read(frame(0)), first);
    EXPECT_EQ(read(frame(1)), second);
}

TEST_F(VoxelEditorSceneIO, MissingOrUnsupportedRigRejectsWholeDocument) {
    IRAsset::Rig rig;
    rig.joints_.resize(2);
    ASSERT_TRUE(save(&rig).ok_);
    ASSERT_TRUE(std::filesystem::remove(m_directory / "named.rig"));
    EXPECT_FALSE(load().ok_);
    EXPECT_TRUE(load().frameSnapshots_.empty());
    rig.joints_[0].parentIndex_ = 1;
    ASSERT_TRUE(IRAsset::saveRig("named", m_directory.string(), rig).ok());
    EXPECT_FALSE(load().ok_);
    EXPECT_FALSE(save(&rig).ok_);
    rig.joints_[0].parentIndex_ = 0;
    rig.joints_[0].rotation_ = IRMath::vec4(0.0f);
    EXPECT_FALSE(save(&rig).ok_);
    rig.joints_[0].rotation_ = {0.0f, 0.0f, 0.0f, 1.0f};
    rig.joints_[0].translation_.x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(save(&rig).ok_);
    rig.joints_[0].translation_.x = 0.0f;
    rig.bindPoints_.resize(1);
    EXPECT_FALSE(save(&rig).ok_);
    ASSERT_TRUE(IRAsset::saveRig("named", m_directory.string(), rig).ok());
    const auto unsupported = load();
    EXPECT_FALSE(unsupported.ok_);
    EXPECT_NE(unsupported.errorMsg_.find("bind points"), std::string::npos);
    EXPECT_TRUE(unsupported.frameSnapshots_.empty());
}

TEST_F(VoxelEditorSceneIO, PublicationRefusalPreservesEveryExistingFrame) {
    ASSERT_TRUE(save().ok_);
    const auto first = read(frame(0));
    const auto second = read(frame(1));
    m_animation.frames_.resize(3);
    m_frames.push_back(m_frames[0]);
    m_frames[0][0].color_.red_ = 99;
    ASSERT_TRUE(std::filesystem::create_directory(frame(2)));
    const auto result = save();
    EXPECT_FALSE(result.ok_);
    EXPECT_EQ(read(frame(0)), first);
    EXPECT_EQ(read(frame(1)), second);
    EXPECT_TRUE(std::filesystem::is_directory(frame(2)));
    EXPECT_FALSE(std::filesystem::exists(m_directory / ".named.saving"));
}

TEST_F(VoxelEditorSceneIO, InvalidSnapshotDoesNotReplaceSavedFrames) {
    ASSERT_TRUE(save().ok_);
    const auto first = read(frame(0));
    m_frames[0][0].color_.red_ = 99;
    m_frames[1].clear();
    EXPECT_FALSE(save().ok_);
    EXPECT_EQ(read(frame(0)), first);
    EXPECT_FALSE(std::filesystem::exists(m_directory / ".named.saving"));
}

TEST_F(VoxelEditorSceneIO, OptionalSummaryObstructionDoesNotFailCanonicalSave) {
    ASSERT_TRUE(std::filesystem::create_directory(frame(0).string() + ".json"));
    ASSERT_TRUE(save().ok_);
    EXPECT_TRUE(load().ok_);
    EXPECT_TRUE(std::filesystem::is_directory(frame(0).string() + ".json"));
}

TEST_F(VoxelEditorSceneIO, RejectsMalformedMetadataBeforeReturningAnyFrames) {
    const std::pair<std::string, std::string> invalid[] = {
        {"frame_count", "0"},
        {"frame_count", "-1"},
        {"frame_count", "2trailing"},
        {"fps", "nan"},
        {"fps", "0"},
        {"fps", "inf"},
        {"fps", "12junk"},
        {"active_frame", "2"},
        {"active_frame", "-1"},
        {"layer_count", "-1"},
        {"layer_count", "257"},
        {"layer_count", "0"},
        {"active_layer_id", "256"},
        {"next_layer_id", "-1"},
        {"sym_enable_x", "2"},
        {"sym_offset_z", "nan"},
        {"layer_0_color_r", "256"},
        {"layer_0_id", "1"},
        {"loop_mode", "unknown"}
    };
    for (const auto &[key, value] : invalid) {
        SCOPED_TRACE(key + "=" + value);
        ASSERT_TRUE(save().ok_);
        rewriteMetadata(key, value);
        const auto result = load();
        EXPECT_FALSE(result.ok_);
        EXPECT_FALSE(result.errorMsg_.empty());
        EXPECT_TRUE(result.frameSnapshots_.empty());
    }
}

TEST_F(VoxelEditorSceneIO, MissingOrDifferentSizedLaterFrameReturnsNoPartialDocument) {
    ASSERT_TRUE(save().ok_);
    ASSERT_TRUE(std::filesystem::remove(frame(1)));
    EXPECT_FALSE(load().ok_);
    EXPECT_TRUE(load().frameSnapshots_.empty());
    IRAsset::DenseVoxelSet different;
    different.boundsMax_ = {2, 1, 1};
    different.voxels_.resize(2);
    ASSERT_TRUE(IRAsset::saveDenseVoxelSet(frame(1).string(), different).ok());
    const auto result = load();
    EXPECT_FALSE(result.ok_);
    EXPECT_TRUE(result.frameSnapshots_.empty());
}

TEST_F(VoxelEditorSceneIO, MissingMetadataLoadsRawVoxelsWithDefaultLayer) {
    IRAsset::DenseVoxelSet raw;
    raw.boundsMax_ = {1, 1, 1};
    raw.voxels_.resize(1);
    ASSERT_TRUE(IRAsset::saveDenseVoxelSet(frame(0).string(), raw).ok());
    const auto result = load();
    ASSERT_TRUE(result.ok_) << result.errorMsg_;
    ASSERT_EQ(result.layers_.size(), 1u);
    EXPECT_EQ(result.layers_[0].id_, 0);
    EXPECT_EQ(result.frameSnapshots_.size(), 1u);
}

TEST_F(VoxelEditorSceneIO, HugeFrameCountFailsAtMissingFrameWithoutAllocatingCountSlots) {
    ASSERT_TRUE(save().ok_);
    rewriteMetadata("frame_count", "2147483647");
    const auto result = load();
    EXPECT_FALSE(result.ok_);
    EXPECT_TRUE(result.frameSnapshots_.empty());
}

} // namespace
