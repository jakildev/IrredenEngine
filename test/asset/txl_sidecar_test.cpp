#include <gtest/gtest.h>

#include <irreden/ir_asset.hpp>

#include <cstdio>
#include <string>

namespace {

const std::string kTmpDir = "/tmp";

TEST(TxlSidecar, RoundTripBindPoints) {
    const std::string name = "ir_test_txl_sidecar_rt";

    IRAsset::TxlSidecar sidecar;
    IRAsset::BindPoint bp0;
    bp0.name_ = "root";
    bp0.boneId_ = 0;
    bp0.offset_ = vec3{1.0f, 2.0f, 3.0f};
    bp0.rotation_ = vec4{1.0f, 0.0f, 0.0f, 0.0f};
    sidecar.bindPoints_.push_back(bp0);

    IRAsset::BindPoint bp1;
    bp1.name_ = "hand_left";
    bp1.boneId_ = 5;
    bp1.offset_ = vec3{-0.5f, 1.2f, 0.0f};
    bp1.rotation_ = vec4{0.7071f, 0.7071f, 0.0f, 0.0f};
    sidecar.bindPoints_.push_back(bp1);

    IRAsset::saveTxlSidecar(name, kTmpDir, sidecar);

    const IRAsset::TxlSidecar loaded = IRAsset::loadTxlSidecar(name, kTmpDir);

    ASSERT_EQ(loaded.bindPoints_.size(), 2u);

    EXPECT_EQ(loaded.bindPoints_[0].name_, "root");
    EXPECT_EQ(loaded.bindPoints_[0].boneId_, 0);
    EXPECT_FLOAT_EQ(loaded.bindPoints_[0].offset_.x, 1.0f);
    EXPECT_FLOAT_EQ(loaded.bindPoints_[0].offset_.y, 2.0f);
    EXPECT_FLOAT_EQ(loaded.bindPoints_[0].offset_.z, 3.0f);
    EXPECT_FLOAT_EQ(loaded.bindPoints_[0].rotation_.x, 1.0f);
    EXPECT_FLOAT_EQ(loaded.bindPoints_[0].rotation_.y, 0.0f);
    EXPECT_FLOAT_EQ(loaded.bindPoints_[0].rotation_.z, 0.0f);
    EXPECT_FLOAT_EQ(loaded.bindPoints_[0].rotation_.w, 0.0f);

    EXPECT_EQ(loaded.bindPoints_[1].name_, "hand_left");
    EXPECT_EQ(loaded.bindPoints_[1].boneId_, 5);
    EXPECT_NEAR(loaded.bindPoints_[1].offset_.x, -0.5f, 1e-5f);
    EXPECT_NEAR(loaded.bindPoints_[1].offset_.y, 1.2f, 1e-5f);
    EXPECT_NEAR(loaded.bindPoints_[1].rotation_.x, 0.7071f, 1e-4f);
    EXPECT_NEAR(loaded.bindPoints_[1].rotation_.y, 0.7071f, 1e-4f);

    EXPECT_TRUE(loaded.componentPackJson_.empty());
    EXPECT_TRUE(loaded.materialRefs_.empty());

    std::remove((kTmpDir + "/" + name + ".txl.json").c_str());
}

TEST(TxlSidecar, RoundTripMaterialRefs) {
    const std::string name = "ir_test_txl_sidecar_matref";

    IRAsset::TxlSidecar sidecar;
    sidecar.materialRefs_.push_back({"wood", 1});
    sidecar.materialRefs_.push_back({"stone", 7});
    sidecar.materialRefs_.push_back({"metal", 255});

    IRAsset::saveTxlSidecar(name, kTmpDir, sidecar);
    const IRAsset::TxlSidecar loaded = IRAsset::loadTxlSidecar(name, kTmpDir);

    ASSERT_EQ(loaded.materialRefs_.size(), 3u);
    EXPECT_EQ(loaded.materialRefs_[0].name_, "wood");
    EXPECT_EQ(loaded.materialRefs_[0].materialId_, 1u);
    EXPECT_EQ(loaded.materialRefs_[1].name_, "stone");
    EXPECT_EQ(loaded.materialRefs_[1].materialId_, 7u);
    EXPECT_EQ(loaded.materialRefs_[2].name_, "metal");
    EXPECT_EQ(loaded.materialRefs_[2].materialId_, 255u);

    std::remove((kTmpDir + "/" + name + ".txl.json").c_str());
}

TEST(TxlSidecar, RoundTripComponentPackJson) {
    const std::string name = "ir_test_txl_sidecar_comppack";

    IRAsset::TxlSidecar sidecar;
    sidecar.componentPackJson_ = R"({"Health":{"max":100},"Speed":{"base":5.0}})";

    IRAsset::saveTxlSidecar(name, kTmpDir, sidecar);
    const IRAsset::TxlSidecar loaded = IRAsset::loadTxlSidecar(name, kTmpDir);

    EXPECT_FALSE(loaded.componentPackJson_.empty());
    EXPECT_NE(loaded.componentPackJson_.find("Health"), std::string::npos);
    EXPECT_NE(loaded.componentPackJson_.find("Speed"), std::string::npos);

    std::remove((kTmpDir + "/" + name + ".txl.json").c_str());
}

TEST(TxlSidecar, MissingFileReturnsEmptyDefault) {
    const IRAsset::TxlSidecar loaded =
        IRAsset::loadTxlSidecar("ir_test_nonexistent_sidecar", kTmpDir);

    EXPECT_TRUE(loaded.empty());
    EXPECT_TRUE(loaded.bindPoints_.empty());
    EXPECT_TRUE(loaded.componentPackJson_.empty());
    EXPECT_TRUE(loaded.materialRefs_.empty());
}

TEST(TxlSidecar, EmptySidecarNotWritten) {
    const std::string name = "ir_test_txl_sidecar_empty";
    const std::string path = kTmpDir + "/" + name + ".txl.json";

    std::remove(path.c_str());

    IRAsset::TxlSidecar sidecar;
    IRAsset::saveTxlSidecar(name, kTmpDir, sidecar);

    FILE *f = fopen(path.c_str(), "r");
    EXPECT_EQ(f, nullptr) << "empty sidecar should not create a file";
    if (f)
        fclose(f);
}

} // namespace
