#include <gtest/gtest.h>

#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_set_lua.hpp>
#include <irreden/voxel/sdf_fill.hpp>

#include <cstddef>
#include <string>

namespace {

using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::EntityAnchor;
using IRMath::Color;
using IRMath::ivec3;

// A 9-cubed grid samples integer centres from -4 through 4; 257 satisfy
// x^2 + y^2 + z^2 <= (3.5 + kSurfaceThreshold)^2.
constexpr std::size_t kRadiusThreePointFiveOccupiedCells = 257;

class LuaVoxelSeam : public testing::Test {
  protected:
    LuaVoxelSeam()
        : m_lua{}
        , m_entityManager{} {
        m_lua.bindLuaDrivenEcs();
        m_lua.registerType<Color, Color(int, int, int, int)>("Color");
        m_lua.registerType<ivec3, ivec3(int, int, int)>("ivec3");
        m_lua.registerTypeFromTraits<C_VoxelSetNew>();
    }

    IREntity::EntityId makeCanvas() {
        return IREntity::createEntity(C_VoxelPool{ivec3(64, 64, 64)});
    }

    static int flat(const C_VoxelSetNew &set, ivec3 cell) {
        return IRMath::index3DtoIndex1D(cell, set.size_);
    }

    static bool maskBit(const C_VoxelSetNew &set, const C_VoxelPool &pool, ivec3 cell) {
        const std::size_t slot = set.voxelStartIdx_ + static_cast<std::size_t>(flat(set, cell));
        return (pool.getActiveMask()[slot / IRComponents::kVoxelActiveMaskBits] >>
                (slot % IRComponents::kVoxelActiveMaskBits)) &
               1u;
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
};

TEST_F(LuaVoxelSeam, LoadsDenseVxs) {
    auto &lua = m_lua.lua();
    lua["assetPath"] = std::string{IR_LUA_VOXEL_ASSET_PATH};

    auto result = lua.safe_script("return IRAsset.loadVoxelSet(assetPath)");
    ASSERT_TRUE(result.valid());

    const C_VoxelSetNew &set = result.get<const C_VoxelSetNew &>();
    auto loaded = IRAsset::loadVoxelSet(IR_LUA_VOXEL_ASSET_PATH);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value_.mode_, IRAsset::VoxelSetMode::DENSE);
    EXPECT_EQ(set.recordCount(), loaded.value_.dense_.voxelCount());
    EXPECT_EQ(set.anchor_, EntityAnchor::CENTER);

    std::size_t expectedOccupied = 0;
    for (const auto &voxel : loaded.value_.dense_.voxels_) {
        expectedOccupied += voxel.color_.alpha_ != 0 ? 1u : 0u;
    }
    std::size_t actualOccupied = 0;
    for (const auto &voxel : set.authoredRecords()) {
        actualOccupied += voxel.color_.alpha_ != 0 ? 1u : 0u;
    }
    EXPECT_EQ(actualOccupied, expectedOccupied);
}

TEST_F(LuaVoxelSeam, SetAndClearRoundTrip) {
    const IREntity::EntityId canvas = makeCanvas();
    auto &lua = m_lua.lua();
    lua["testCanvas"] = canvas;

    auto result = lua.safe_script(R"lua(
        local set = C_VoxelSetNew.new(
            ivec3.new(3, 3, 3),
            Color.new(0, 0, 0, 0),
            IRComponent.EntityAnchor.CORNER,
            testCanvas
        )
        set:setVoxel(1, 1, 1, Color.new(12, 34, 56, 255))
        set:clearVoxel(1, 1, 1)
        set:setVoxel(2, 1, 1, Color.new(90, 80, 70, 0))
        return set
    )lua");
    ASSERT_TRUE(result.valid());

    const C_VoxelSetNew &set = result.get<const C_VoxelSetNew &>();
    EXPECT_EQ(set.voxels_[flat(set, {1, 1, 1})].color_.alpha_, 0);
    const auto &written = set.voxels_[flat(set, {2, 1, 1})].color_;
    EXPECT_EQ(written.red_, 90);
    EXPECT_EQ(written.green_, 80);
    EXPECT_EQ(written.blue_, 70);
    EXPECT_EQ(written.alpha_, 255);
}

TEST_F(LuaVoxelSeam, SdfFillMatchesEditor) {
    const IREntity::EntityId canvas = makeCanvas();
    auto &lua = m_lua.lua();
    lua["testCanvas"] = canvas;

    auto result = lua.safe_script(R"lua(
        local set = C_VoxelSetNew.new(
            ivec3.new(9, 9, 9),
            Color.new(0, 0, 0, 0),
            IRComponent.EntityAnchor.CENTER,
            testCanvas
        )
        set:fillSdf(IRShape.SPHERE, { 3.5, 0.0, 0.0, 0.0 }, Color.new(40, 120, 220, 255))
        return set
    )lua");
    ASSERT_TRUE(result.valid());
    const C_VoxelSetNew &luaSet = result.get<const C_VoxelSetNew &>();

    // The voxel editor calls this shared overload, so parity is by construction
    // at the engine seam rather than by linking the editor executable into this test.
    C_VoxelSetNew editorSet{ivec3(9, 9, 9), Color{0, 0, 0, 0}, EntityAnchor::CENTER, canvas};
    IRPrefab::Voxel::fillSdf(
        editorSet,
        IRMath::SDF::ShapeType::SPHERE,
        IRMath::vec4(3.5f, 0.0f, 0.0f, 0.0f),
        Color{40, 120, 220, 255}
    );

    ASSERT_EQ(luaSet.voxels_.size(), editorSet.voxels_.size());
    std::size_t occupied = 0;
    for (const auto &voxel : luaSet.voxels_) {
        occupied += voxel.color_.alpha_ != 0 ? 1u : 0u;
    }
    ASSERT_GT(occupied, 0u);
    ASSERT_LT(occupied, 9u * 9u * 9u);
    ASSERT_EQ(occupied, kRadiusThreePointFiveOccupiedCells);

    for (std::size_t i = 0; i < luaSet.voxels_.size(); ++i) {
        EXPECT_EQ(luaSet.voxels_[i].color_.alpha_, editorSet.voxels_[i].color_.alpha_) << i;
        EXPECT_EQ(luaSet.voxels_[i].flags_, editorSet.voxels_[i].flags_) << i;
    }
}

TEST_F(LuaVoxelSeam, BatchResyncsOnce) {
    const IREntity::EntityId canvas = makeCanvas();
    auto &lua = m_lua.lua();
    lua["testCanvas"] = canvas;

    auto result = lua.safe_script(R"lua(
        local set = C_VoxelSetNew.new(
            ivec3.new(4, 4, 4),
            Color.new(0, 0, 0, 0),
            IRComponent.EntityAnchor.CORNER,
            testCanvas
        )
        set:batch(function(batch)
            batch:setVoxel(1, 1, 1, Color.new(200, 100, 50, 255))
            batch:setVoxel(2, 1, 1, Color.new(200, 100, 50, 255))
            batch:setVoxel(0, 0, 0, Color.new(200, 100, 50, 255))
            batch:clearVoxel(0, 0, 0)
        end)
        return set
    )lua");
    ASSERT_TRUE(result.valid());

    const C_VoxelSetNew &set = result.get<const C_VoxelSetNew &>();
    EXPECT_EQ(set.voxels_[flat(set, {0, 0, 0})].color_.alpha_, 0);
    EXPECT_EQ(set.voxels_[flat(set, {1, 1, 1})].color_.alpha_, 255);
    EXPECT_EQ(set.voxels_[flat(set, {2, 1, 1})].color_.alpha_, 255);
    EXPECT_NE(
        set.voxels_[flat(set, {2, 1, 1})].flags_ & IRComponents::VoxelFlags::kFaceOccludedNegX,
        0u
    );
    EXPECT_NE(
        set.voxels_[flat(set, {1, 1, 1})].flags_ & IRComponents::VoxelFlags::kFaceOccludedPosX,
        0u
    );

    const auto &pool = IREntity::getComponent<C_VoxelPool>(canvas);
    EXPECT_FALSE(maskBit(set, pool, {0, 0, 0}));
    EXPECT_TRUE(maskBit(set, pool, {1, 1, 1}));
    EXPECT_TRUE(maskBit(set, pool, {2, 1, 1}));
}

TEST_F(LuaVoxelSeam, BatchHandleExpiresAfterCallback) {
    const IREntity::EntityId canvas = makeCanvas();
    auto &lua = m_lua.lua();
    lua["testCanvas"] = canvas;

    auto result = lua.safe_script(R"lua(
        local set = C_VoxelSetNew.new(
            ivec3.new(2, 2, 2),
            Color.new(0, 0, 0, 0),
            IRComponent.EntityAnchor.CORNER,
            testCanvas
        )
        local saved
        set:batch(function(batch)
            saved = batch
        end)
        local ok, message = pcall(
            saved.setVoxel,
            saved,
            0,
            0,
            0,
            Color.new(1, 2, 3, 255)
        )
        return not ok and
            string.find(message, "batch handle used outside its callback", 1, true) ~= nil
    )lua");
    ASSERT_TRUE(result.valid());
    EXPECT_TRUE(result.get<bool>());
}

TEST_F(LuaVoxelSeam, OutOfRangeWritesRaise) {
    const IREntity::EntityId canvas = makeCanvas();
    auto &lua = m_lua.lua();
    lua["testCanvas"] = canvas;

    auto result = lua.safe_script(R"lua(
        local set = C_VoxelSetNew.new(
            ivec3.new(2, 2, 2),
            Color.new(0, 0, 0, 0),
            IRComponent.EntityAnchor.CORNER,
            testCanvas
        )
        return pcall(set.setVoxel, set, 2, 0, 0, Color.new(1, 2, 3, 255))
    )lua");
    ASSERT_TRUE(result.valid());
    EXPECT_FALSE(result.get<bool>());
}

} // namespace
