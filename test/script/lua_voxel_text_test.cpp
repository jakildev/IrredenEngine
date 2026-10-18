#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/voxel_font.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRMath::Color;
using IRMath::ivec3;
using IRMath::vec3;

int glyphPopcount(char c) {
    int lit = 0;
    for (const std::uint8_t row : *IRRender::getVoxelGlyph(c)) {
        lit += static_cast<int>(std::bitset<8>(row).count());
    }
    return lit;
}

class LuaVoxelText : public testing::Test {
  protected:
    LuaVoxelText() {
        m_lua.bindLuaDrivenEcs();
        m_lua.registerType<Color, Color(int, int, int, int)>("Color");
        m_lua.registerType<ivec3, ivec3(int, int, int)>("ivec3");
        m_lua.registerType<vec3, vec3(float, float, float)>("vec3");
        IREntity::setName(IREntity::createEntity(), "camera");
        m_canvas = IREntity::createEntity(C_VoxelPool{ivec3(64, 64, 64)});
        m_lua.lua()["testCanvas"] = m_canvas;
    }

    std::size_t activeMaskPopcount() {
        const auto &mask = IREntity::getComponent<C_VoxelPool>(m_canvas).getActiveMask();
        std::size_t count = 0;
        for (const auto word : mask) {
            count += std::bitset<64>(static_cast<unsigned long long>(word)).count();
        }
        return count;
    }

    static int activeVoxels(const C_VoxelSetNew &set) {
        int active = 0;
        for (const auto &voxel : set.voxels_) {
            active += voxel.color_.alpha_ != 0 ? 1 : 0;
        }
        return active;
    }

    bool isLive(IREntity::EntityId entity) {
        const IREntity::EntityRecord *record = IREntity::getEntityManager().findRecord(entity);
        return record != nullptr && record->archetypeNode != nullptr;
    }

    std::size_t liveEntityCount() {
        return IREntity::getEntityManager().getLiveEntityCount();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
    IREntity::EntityId m_canvas = IREntity::kNullEntity;
};

TEST_F(LuaVoxelText, BuildsCountsAndRemovesATitle) {
    const std::size_t popcountBefore = activeMaskPopcount();
    auto result = m_lua.lua().safe_script(R"lua(
        local root, letters = IRText.createVoxel("ABC", { canvas = testCanvas })
        local children = {}
        IREntity.forEachChild(root, function(child) children[#children + 1] = child end)
        return root, letters, children
    )lua");
    ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    const auto root = result.get<IREntity::EntityId>(0);
    EXPECT_EQ(result.get<int>(1), 3);
    const sol::table childTable = result.get<sol::table>(2);
    ASSERT_EQ(childTable.size(), 3u);

    std::vector<IREntity::EntityId> children;
    std::size_t letterVoxels = 0;
    std::vector<int> counts;
    for (std::size_t i = 1; i <= childTable.size(); ++i) {
        const auto child = childTable.get<IREntity::EntityId>(i);
        children.push_back(child);
        const int active = activeVoxels(IREntity::getComponent<C_VoxelSetNew>(child));
        counts.push_back(active);
        letterVoxels += static_cast<std::size_t>(active);
    }
    std::vector<int> expected;
    for (const char c : std::string{"ABC"}) {
        expected.push_back(glyphPopcount(c) * IRRender::kVoxelGlyphRowVoxels * 2);
    }
    std::sort(counts.begin(), counts.end());
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(counts, expected) << "popcount(glyph) * 2 rows * depth 2 per letter";
    EXPECT_GT(letterVoxels, 0u);
    EXPECT_EQ(activeMaskPopcount() - popcountBefore, letterVoxels);

    m_lua.lua()["titleRoot"] = root;
    auto removed = m_lua.lua().safe_script("IRText.removeVoxel(titleRoot)");
    ASSERT_TRUE(removed.valid()) << sol::error{removed}.what();
    IREntity::getEntityManager().destroyMarkedEntities();
    EXPECT_FALSE(isLive(root));
    for (const IREntity::EntityId child : children) {
        EXPECT_FALSE(isLive(child));
    }
    EXPECT_EQ(activeMaskPopcount(), popcountBefore);
}

TEST_F(LuaVoxelText, ASpaceAdvancesWithoutALetter) {
    auto result = m_lua.lua().safe_script(R"lua(
        local root, letters = IRText.createVoxel("A C", { canvas = testCanvas })
        return letters
    )lua");
    ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    EXPECT_EQ(result.get<int>(), 2);
}

TEST_F(LuaVoxelText, AcceptsEveryPathFormAndOption) {
    auto result = m_lua.lua().safe_script(R"lua(
        local counts = {}
        local _, a = IRText.createVoxel("AB", {
            canvas = testCanvas, origin = { 1, 2, 3 }, color = Color.new(10, 20, 30, 255),
            depth = 1, scale = 2, path = math.pi,
        })
        local _, b = IRText.createVoxel("AB", {
            canvas = testCanvas, origin = vec3.new(0, 0, 0), path = { 0.0, math.pi },
        })
        local _, c = IRText.createVoxel("AB", {
            canvas = testCanvas, origin = ivec3.new(0, 0, 0),
            path = { { x = 4, y = 0, z = 0 }, vec3.new(4, 8, 0) },
        })
        local _, d = IRText.createVoxel("AB", { canvas = testCanvas, color = { 1, 2, 3 } })
        return a + b + c + d
    )lua");
    ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    EXPECT_EQ(result.get<int>(), 8);
}

TEST_F(LuaVoxelText, MalformedInputRaisesBeforeAnyEntityExists) {
    const char *cases[] = {
        R"lua(IRText.createVoxel("AB", { canvas = testCanvas, depth = 0 }))lua",
        R"lua(IRText.createVoxel("AB", { canvas = testCanvas, scale = 1.5 }))lua",
        R"lua(IRText.createVoxel("AB", { canvas = testCanvas, color = "red" }))lua",
        R"lua(IRText.createVoxel("AB", { canvas = testCanvas, path = { 0.5, { 1, 2, 3 } } }))lua",
        R"lua(IRText.createVoxel("AB", { canvas = testCanvas, path = {} }))lua",
        R"lua(IRText.createVoxel("A\nB", { canvas = testCanvas }))lua",
        R"lua(IRText.createVoxel("AB", { canvas = testCanvas, size = 2 }))lua",
        R"lua(IRText.createVoxel("AB", { canvas = testCanvas, path = { "east" } }))lua",
        R"lua(IRText.createVoxel("AB", { canvas = testCanvas, path = { { 0, 0, 0 } } }))lua",
        R"lua(IRText.createVoxel("AB", { canvas = testCanvas, path = { { 2, 2, 2 } } }))lua",
        R"lua(IRText.createVoxel("AB", { canvas = testCanvas, origin = { 0 / 0, 0, 0 } }))lua",
        R"lua(IRText.createVoxel("AB", { canvas = 999999 }))lua",
        R"lua(IRText.createVoxel("AB", 7))lua",
    };
    for (const char *script : cases) {
        const std::size_t entitiesBefore = liveEntityCount();
        m_lua.lua()["probe"] = m_lua.lua().load(script).get<sol::protected_function>();
        auto result = m_lua.lua().safe_script(R"lua(
            local ok, message = pcall(probe)
            return ok, tostring(message)
        )lua");
        ASSERT_TRUE(result.valid()) << script;
        EXPECT_FALSE(result.get<bool>(0)) << script;
        // sol2 prefixes every raised sol::error with its own tag.
        constexpr std::string_view kSolErrorTag = "lua: error: ";
        std::string message = result.get<std::string>(1);
        if (message.rfind(kSolErrorTag, 0) == 0) {
            message.erase(0, kSolErrorTag.size());
        }
        EXPECT_EQ(message.rfind("IRText.createVoxel: ", 0), 0u) << script << " -> " << message;
        EXPECT_EQ(liveEntityCount(), entitiesBefore) << script;
    }
}

TEST_F(LuaVoxelText, LettersFaceTheLiveCameraYaw) {
    auto readTranslations = [&]() {
        auto result = m_lua.lua().safe_script(R"lua(
            local root = IRText.createVoxel("AB", { canvas = testCanvas })
            local children = {}
            IREntity.forEachChild(root, function(child) children[#children + 1] = child end)
            return children
        )lua");
        EXPECT_TRUE(result.valid()) << sol::error{result}.what();
        const sol::table children = result;
        std::vector<vec3> translations;
        for (std::size_t i = 1; i <= children.size(); ++i) {
            translations.push_back(
                IREntity::getComponent<IRComponents::C_LocalTransform>(
                    children.get<IREntity::EntityId>(i)
                )
                    .translation_
            );
        }
        std::sort(translations.begin(), translations.end(), [](vec3 a, vec3 b) {
            return a.x + a.y < b.x + b.y;
        });
        return translations;
    };
    const std::vector<vec3> atYawZero = readTranslations();
    ASSERT_EQ(atYawZero.size(), 2u);
    EXPECT_EQ(atYawZero[1] - atYawZero[0], vec3(0.0f, 12.0f, 0.0f)) << "default path: world +Y";

    IRPrefab::Camera::setYaw(IRMath::kHalfPi);
    const std::vector<vec3> atYawNinety = readTranslations();
    ASSERT_EQ(atYawNinety.size(), 2u);
    EXPECT_EQ(atYawNinety[1] - atYawNinety[0], vec3(12.0f, 0.0f, 0.0f))
        << "default path at yaw 90: world -X, B first in x + y order";
}

TEST_F(LuaVoxelText, CreatorEntriesOnIRTextSurvive) {
    auto result = m_lua.lua().safe_script(R"lua(
        IRText.create = function() return "creation" end
        return IRText.create(), type(IRText.createVoxel), type(IRText.measure)
    )lua");
    ASSERT_TRUE(result.valid());
    m_lua.bindLuaText();
    auto after = m_lua.lua().safe_script(
        "return IRText.create(), type(IRText.createVoxel), type(IRText.removeVoxel)"
    );
    ASSERT_TRUE(after.valid());
    EXPECT_EQ(after.get<std::string>(0), "creation");
    EXPECT_EQ(after.get<std::string>(1), "function");
    EXPECT_EQ(after.get<std::string>(2), "function");
}

} // namespace
