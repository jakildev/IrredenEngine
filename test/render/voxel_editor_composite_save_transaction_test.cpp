#include "../../creations/editors/voxel_editor/composite_save_transaction.hpp"
#include "../../creations/editors/voxel_editor/entity_scene.hpp"

#include <gtest/gtest.h>

#include <irreden/entity/entity_manager.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/system/system_manager.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using IRVoxelEditor::CompositeSaveTransaction;

class VoxelEditorCompositeSave : public testing::Test {
  protected:
    void SetUp() override {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_directory =
            std::filesystem::temp_directory_path() / ("ir_composite_save_" + std::to_string(stamp));
        m_owned = std::filesystem::create_directory(m_directory);
        ASSERT_TRUE(m_owned);
    }

    void TearDown() override {
        if (m_owned) {
            std::error_code error;
            std::filesystem::remove_all(m_directory, error);
        }
    }

    static void write(const std::filesystem::path &path, const std::string &text) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file);
        file << text;
        file.close();
        ASSERT_TRUE(file);
    }

    static std::string read(const std::filesystem::path &path) {
        std::ifstream file(path, std::ios::binary);
        EXPECT_TRUE(file);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    static void
    stage(CompositeSaveTransaction &transaction, const std::string &name, const std::string &text) {
        const auto path = transaction.stageFile(name);
        ASSERT_TRUE(path) << transaction.error();
        write(*path, text);
    }

    std::filesystem::path m_directory;
    bool m_owned = false;
};

TEST_F(VoxelEditorCompositeSave, PublishesBundleAndPreservesUnrelatedDocuments) {
    write(m_directory / "scene_part.vxs", "old voxels");
    write(m_directory / "scene.recipes.lua", "old recipe");
    write(m_directory / "scene.prefab.lua", "old manifest");
    write(m_directory / "other.prefab.lua", "unrelated");
    {
        CompositeSaveTransaction transaction;
        ASSERT_TRUE(transaction.begin(m_directory, "scene")) << transaction.error();
        stage(transaction, "scene_part.vxs", "new voxels");
        write(m_directory / ".scene.saving" / "staged" / "scene_part.vxs.json", "new summary");
        stage(transaction, "scene.recipes.lua", "new recipe");
        stage(transaction, "scene.prefab.lua", "new manifest");
        EXPECT_EQ(read(m_directory / "scene_part.vxs"), "old voxels");
        EXPECT_EQ(read(m_directory / "scene.prefab.lua"), "old manifest");
        ASSERT_TRUE(transaction.publish()) << transaction.error();
        ASSERT_TRUE(transaction.publishOptionalFile("scene_part.vxs.json")) << transaction.error();
        EXPECT_EQ(read(m_directory / "scene_part.vxs"), "new voxels");
        EXPECT_EQ(read(m_directory / "scene_part.vxs.json"), "new summary");
        EXPECT_EQ(read(m_directory / "scene.recipes.lua"), "new recipe");
        EXPECT_EQ(read(m_directory / "scene.prefab.lua"), "new manifest");
        EXPECT_EQ(read(m_directory / "other.prefab.lua"), "unrelated");
    }
    EXPECT_FALSE(std::filesystem::exists(m_directory / ".scene.saving"));
}

TEST_F(VoxelEditorCompositeSave, FailedManifestPublishRestoresReplacedAndRemovedFiles) {
    write(m_directory / "scene_part.vxs", "old voxels");
    write(m_directory / "scene.recipes.lua", "old recipe");
    write(m_directory / "scene.prefab.lua", "old manifest");
    write(m_directory / "other.prefab.lua", "unrelated");
    {
        CompositeSaveTransaction transaction;
        ASSERT_TRUE(transaction.begin(m_directory, "scene"));
        stage(transaction, "scene_part.vxs", "new voxels");
        stage(transaction, "scene_new.vxs", "new part");
        ASSERT_TRUE(transaction.removeFile("scene.recipes.lua"));
        stage(transaction, "scene.prefab.lua", "new manifest");
        ASSERT_TRUE(
            std::filesystem::remove(m_directory / ".scene.saving" / "staged" / "scene.prefab.lua")
        );
        EXPECT_FALSE(transaction.publish());
        EXPECT_NE(transaction.error().find("could not publish"), std::string::npos);
        EXPECT_EQ(read(m_directory / "scene_part.vxs"), "old voxels");
        EXPECT_EQ(read(m_directory / "scene.recipes.lua"), "old recipe");
        EXPECT_EQ(read(m_directory / "scene.prefab.lua"), "old manifest");
        EXPECT_EQ(read(m_directory / "other.prefab.lua"), "unrelated");
        EXPECT_FALSE(std::filesystem::exists(m_directory / "scene_new.vxs"));
    }
    EXPECT_FALSE(std::filesystem::exists(m_directory / ".scene.saving"));
}

TEST_F(VoxelEditorCompositeSave, AbandonedStagingLeavesTheOldDocumentUntouched) {
    write(m_directory / "scene_part.vxs", "old voxels");
    write(m_directory / "scene.prefab.lua", "old manifest");
    {
        CompositeSaveTransaction transaction;
        ASSERT_TRUE(transaction.begin(m_directory, "scene"));
        stage(transaction, "scene_part.vxs", "new voxels");
    }
    EXPECT_EQ(read(m_directory / "scene_part.vxs"), "old voxels");
    EXPECT_EQ(read(m_directory / "scene.prefab.lua"), "old manifest");
    EXPECT_FALSE(std::filesystem::exists(m_directory / ".scene.saving"));
}

TEST_F(VoxelEditorCompositeSave, RemovalIsPublishedWithTheManifest) {
    write(m_directory / "scene.recipes.lua", "old recipe");
    {
        CompositeSaveTransaction transaction;
        ASSERT_TRUE(transaction.begin(m_directory, "scene"));
        ASSERT_TRUE(transaction.removeFile("scene.recipes.lua"));
        stage(transaction, "scene.prefab.lua", "new manifest");
        ASSERT_TRUE(transaction.publish()) << transaction.error();
    }
    EXPECT_FALSE(std::filesystem::exists(m_directory / "scene.recipes.lua"));
    EXPECT_EQ(read(m_directory / "scene.prefab.lua"), "new manifest");
}

TEST_F(VoxelEditorCompositeSave, RefusesDirectoriesWithoutTouchingAnyDocumentFiles) {
    write(m_directory / "scene_part.vxs", "old voxels");
    ASSERT_TRUE(std::filesystem::create_directory(m_directory / "scene.recipes.lua"));
    write(m_directory / "scene.recipes.lua" / "unrelated.txt", "keep");
    CompositeSaveTransaction transaction;
    ASSERT_TRUE(transaction.begin(m_directory, "scene"));
    stage(transaction, "scene_part.vxs", "new voxels");
    ASSERT_TRUE(transaction.removeFile("scene.recipes.lua"));
    stage(transaction, "scene.prefab.lua", "new manifest");
    EXPECT_FALSE(transaction.publish());
    EXPECT_EQ(read(m_directory / "scene_part.vxs"), "old voxels");
    EXPECT_EQ(read(m_directory / "scene.recipes.lua" / "unrelated.txt"), "keep");
    EXPECT_FALSE(std::filesystem::exists(m_directory / "scene.prefab.lua"));
}

TEST_F(VoxelEditorCompositeSave, ExistingTransactionIsPreservedForRecovery) {
    ASSERT_TRUE(std::filesystem::create_directory(m_directory / ".scene.saving"));
    write(m_directory / ".scene.saving" / "recovery.txt", "keep");
    {
        CompositeSaveTransaction transaction;
        EXPECT_FALSE(transaction.begin(m_directory, "scene"));
    }
    EXPECT_EQ(read(m_directory / ".scene.saving" / "recovery.txt"), "keep");
}

TEST_F(VoxelEditorCompositeSave, NamesCannotEscapeTheDocumentDirectory) {
    for (const std::string name : {"", ".", "..", "../other", "..\\other", "C:other", "name."}) {
        SCOPED_TRACE(name);
        CompositeSaveTransaction transaction;
        EXPECT_FALSE(transaction.begin(m_directory, name));
    }
    CompositeSaveTransaction transaction;
    ASSERT_TRUE(transaction.begin(m_directory, "scene"));
    EXPECT_FALSE(transaction.stageFile("../escaped.vxs"));
    EXPECT_FALSE(transaction.removeFile("..\\escaped.vxs"));
    ASSERT_TRUE(transaction.stageFile("scene.vxs"));
    EXPECT_FALSE(transaction.stageFile("scene.vxs"));
    EXPECT_FALSE(transaction.begin(m_directory, "other"));
    EXPECT_FALSE(std::filesystem::exists(m_directory / ".other.saving"));
}

TEST_F(VoxelEditorCompositeSave, OptionalSummaryFailurePreservesCanonicalSaveAndUnrelatedData) {
    ASSERT_TRUE(std::filesystem::create_directory(m_directory / "scene_part.vxs.json"));
    write(m_directory / "scene_part.vxs.json" / "unrelated.txt", "keep");
    CompositeSaveTransaction transaction;
    ASSERT_TRUE(transaction.begin(m_directory, "scene"));
    stage(transaction, "scene_part.vxs", "new voxels");
    stage(transaction, "scene.prefab.lua", "new manifest");
    write(m_directory / ".scene.saving" / "staged" / "scene_part.vxs.json", "summary");
    ASSERT_TRUE(transaction.publish()) << transaction.error();
    EXPECT_FALSE(transaction.publishOptionalFile("scene_part.vxs.json"));
    EXPECT_EQ(read(m_directory / "scene_part.vxs"), "new voxels");
    EXPECT_EQ(read(m_directory / "scene.prefab.lua"), "new manifest");
    EXPECT_EQ(read(m_directory / "scene_part.vxs.json" / "unrelated.txt"), "keep");
}

TEST_F(VoxelEditorCompositeSave, OptionalSummaryPublishFailureRestoresTheOldSummary) {
    write(m_directory / "scene_part.vxs.json", "old summary");
    CompositeSaveTransaction transaction;
    ASSERT_TRUE(transaction.begin(m_directory, "scene"));
    stage(transaction, "scene_part.vxs", "new voxels");
    stage(transaction, "scene.prefab.lua", "new manifest");
    ASSERT_TRUE(transaction.publish()) << transaction.error();
    EXPECT_FALSE(transaction.publishOptionalFile("scene_part.vxs.json"));
    EXPECT_EQ(read(m_directory / "scene_part.vxs.json"), "old summary");
    EXPECT_EQ(read(m_directory / "scene_part.vxs"), "new voxels");
    EXPECT_EQ(read(m_directory / "scene.prefab.lua"), "new manifest");
}

TEST_F(VoxelEditorCompositeSave, EntitySceneRoundTripKeepsHierarchyVoxelsAndRecipeBindings) {
    IRScript::LuaScript script;
    IREntity::EntityManager entityManager;
    IRSystem::SystemManager systemManager;
    script.bindLuaDrivenEcs();
    IRVoxelEditor::EntityScene scene;
    scene.begin();

    const auto rootPart = scene.addShapePart(
        {IRMath::SDF::ShapeType::BOX, IRMath::vec4(1.0f), IRMath::Color{120, 130, 140, 255}, 0},
        IRComponents::C_LocalTransform{IRMath::vec3(2.0f, 0.0f, 0.0f)}
    );
    const auto child = IREntity::createEntity(
        IRComponents::C_LocalTransform{IRMath::vec3(0.0f, 3.0f, 0.0f)},
        IRComponents::C_RotationMode{IRComponents::RotationMode::GRID},
        IRComponents::C_VoxelSetNew{
            IRComponents::C_VoxelSetNew::StagedInit{},
            IRMath::ivec3(1),
            IRMath::ivec3(0),
            {IRComponents::C_Voxel{IRMath::Color{20, 40, 60, 255}}},
            IREntity::kNullEntity,
            IRComponents::EntityAnchor::CENTER
        }
    );
    IREntity::setParent(child, rootPart);
    IRVoxelEditor::EditorPart part;
    part.entity_ = child;
    part.id_ = "child";
    part.parentId_ = "part_0";
    part.recipeBinding_ = IRVoxelEditor::PartRecipeBinding{
        "shape",
        {2.0f},
        IRMath::Color{20, 40, 60, 255},
        {{IRMath::ivec3(0), "body"}}
    };
    scene.parts().push_back(std::move(part));
    const std::vector<IRVoxelEditor::ModuleRecipe> recipes{
        {"shape", {{"radius", IRVoxelEditor::RecipeParamType::INT, 1.0f, 1.0f, 4.0f}}, {"body"}, {}}
    };

    const auto relativeDirectory = std::filesystem::relative(m_directory);
    const auto saved = scene.save(relativeDirectory.string(), "scene", recipes);
    ASSERT_TRUE(saved.ok_) << saved.error_;
    EXPECT_TRUE(std::filesystem::is_regular_file(m_directory / "scene_child.vxs.json"));
    EXPECT_EQ(read(m_directory / "scene.prefab.lua").find(".saving"), std::string::npos);
    const auto manifest =
        IRPrefab::Prefab::readManifest(script, (m_directory / "scene.prefab.lua").string());
    ASSERT_TRUE(manifest.ok()) << manifest.error_;
    EXPECT_EQ(manifest.description_->version_, 3);
    ASSERT_EQ(manifest.description_->parts_.size(), 2u);
    EXPECT_EQ(manifest.description_->parts_[1].parentId_, "part_0");
    EXPECT_EQ(
        std::filesystem::path(manifest.description_->parts_[1].voxelRef_).lexically_normal(),
        (relativeDirectory / "scene_child.vxs").lexically_normal()
    );

    const auto savedVoxels = read(m_directory / "scene_child.vxs");
    const auto savedManifest = read(m_directory / "scene.prefab.lua");
    const auto savedRecipes = read(m_directory / "scene.recipes.lua");
    scene.findPart("child")->parentId_ = "missing";
    scene.findPart("child")->recipeBinding_->values_[0] = 3.0f;
    const auto rejected = scene.save(relativeDirectory.string(), "scene", recipes);
    EXPECT_FALSE(rejected.ok_);
    EXPECT_EQ(read(m_directory / "scene_child.vxs"), savedVoxels);
    EXPECT_EQ(read(m_directory / "scene.prefab.lua"), savedManifest);
    EXPECT_EQ(read(m_directory / "scene.recipes.lua"), savedRecipes);
    EXPECT_FALSE(std::filesystem::exists(m_directory / ".scene.saving"));

    const auto loaded = scene.load(script, relativeDirectory.string(), "scene", recipes);
    ASSERT_TRUE(loaded.ok_) << loaded.error_;
    const auto *loadedChild = scene.findPart("child");
    ASSERT_NE(loadedChild, nullptr);
    EXPECT_EQ(loadedChild->parentId_, "part_0");
    ASSERT_TRUE(loadedChild->recipeBinding_);
    EXPECT_EQ(loadedChild->recipeBinding_->values_, std::vector<float>{2.0f});
    ASSERT_EQ(loadedChild->recipeBinding_->mask_.size(), 1u);
    EXPECT_EQ(loadedChild->recipeBinding_->mask_[0].className_, "body");
    const auto dense = IRAsset::loadDenseVoxelSet((m_directory / "scene_child.vxs").string());
    ASSERT_TRUE(dense.ok());
    ASSERT_EQ(dense.value_.dense_.voxels_.size(), 1u);
    const auto color = dense.value_.dense_.voxels_[0].color_;
    EXPECT_EQ(color.red_, 20);
    EXPECT_EQ(color.green_, 40);
    EXPECT_EQ(color.blue_, 60);
    EXPECT_EQ(color.alpha_, 255);
}

} // namespace
