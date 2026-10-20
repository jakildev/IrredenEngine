#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/render/components/component_text_style_lua.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <string>

namespace {

using IRComponents::C_TextStyle;
using IRRender::VoxelTextFacing;

// The voxel-letter fields of C_TextStyle and IRText.VoxelFacing, driven
// through Lua.
//
// m_lua is declared FIRST so it is destroyed LAST (test/CLAUDE.md).
class LuaTextStyleVoxel : public testing::Test {
  protected:
    LuaTextStyleVoxel() {
        m_lua.bindLuaDrivenEcs();
        m_lua.registerType<IRMath::Color, IRMath::Color(int, int, int, int)>(
            "Color",
            "red",
            &IRMath::Color::red_
        );
        m_lua.registerTypeFromTraits<C_TextStyle>();
    }

    C_TextStyle run(const char *script) {
        auto result = m_lua.lua().safe_script(script, sol::script_pass_on_error);
        EXPECT_TRUE(result.valid()) << (result.valid() ? "" : result.get<sol::error>().what());
        return result.valid() ? result.get<C_TextStyle>() : C_TextStyle{};
    }

    std::string error(const char *script) {
        auto result = m_lua.lua().safe_script(script, sol::script_pass_on_error);
        EXPECT_FALSE(result.valid());
        if (result.valid()) {
            return {};
        }
        sol::error err = result;
        return err.what();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
};

TEST_F(LuaTextStyleVoxel, FieldsRoundTripAndDefaultToFlatText) {
    EXPECT_EQ(run("return C_TextStyle.new()").voxel_.voxelSize_, 0);

    const C_TextStyle style = run(R"lua(
        local s = C_TextStyle.new()
        s.voxelSize = 3
        s.voxelDepth = 2
        s.voxelFacing = IRText.VoxelFacing.Y_FACE
        s.faceX = Color.new(10, 20, 30, 255)
        s.faceY = Color.new(40, 50, 60, 255)
        s.faceZ = Color.new(70, 80, 90, 255)
        assert(s.voxelSize == 3 and s.voxelDepth == 2)
        assert(s.voxelFacing == IRText.VoxelFacing.Y_FACE)
        assert(s.faceY.red == 40)
        return s
    )lua");
    EXPECT_EQ(style.voxel_.voxelSize_, 3);
    EXPECT_EQ(style.voxel_.depth_, 2);
    EXPECT_EQ(style.voxel_.facing_, VoxelTextFacing::Y_FACE);
    EXPECT_EQ(style.faceColors_.x_.red_, 10);
    EXPECT_EQ(style.faceColors_.y_.green_, 50);
    EXPECT_EQ(style.faceColors_.z_.blue_, 90);
}

TEST_F(LuaTextStyleVoxel, ShadeFacesDerivesAllThreeFromTheTop) {
    const C_TextStyle style = run(R"lua(
        local s = C_TextStyle.new()
        s:shadeFaces(Color.new(200, 100, 50, 255))
        return s
    )lua");
    const IRRender::VoxelFaceColors expected =
        IRRender::shadedVoxelFaceColors(IRMath::Color{200, 100, 50, 255});
    EXPECT_EQ(style.faceColors_.x_.toPackedRGBA(), expected.x_.toPackedRGBA());
    EXPECT_EQ(style.faceColors_.y_.toPackedRGBA(), expected.y_.toPackedRGBA());
    EXPECT_EQ(style.faceColors_.z_.toPackedRGBA(), expected.z_.toPackedRGBA());
}

TEST_F(LuaTextStyleVoxel, OutOfRangeValuesRaise) {
    EXPECT_NE(
        error("C_TextStyle.new().voxelSize = 17").find("C_TextStyle.voxelSize must be 0..16"),
        std::string::npos
    );
    EXPECT_NE(
        error("C_TextStyle.new().voxelSize = -1").find("C_TextStyle.voxelSize"),
        std::string::npos
    );
    EXPECT_NE(
        error("C_TextStyle.new().voxelDepth = 0").find("C_TextStyle.voxelDepth must be 1..16"),
        std::string::npos
    );
    EXPECT_NE(
        error("C_TextStyle.new().voxelFacing = 3").find("C_TextStyle.voxelFacing: facing must be"),
        std::string::npos
    );
    EXPECT_NE(
        error("C_TextStyle.new().voxelFacing = 'CAMERA'").find("facing must be"),
        std::string::npos
    );
}

} // namespace
