// The Lua seam of C_LodZoomProfile: attach through `IREntity.addLuaComponent`
// and a prefab declaration, read back through `threshold` / `hysteresis`,
// replace through `set`, and the same validation as the C++ surface on every
// path, each leaving the previous profile in place.

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/render/components/component_lod_zoom_profile.hpp>
#include <irreden/render/components/component_lod_zoom_profile_lua.hpp>
#include <irreden/render/lod_utils.hpp>
#include <irreden/render/lod_zoom_policy.hpp>
#include <irreden/render/systems/system_lod_update.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/script/prefab_api.hpp>

#include <fstream>
#include <optional>
#include <string>

namespace {

using IRComponents::C_LodZoomProfile;
using IRRender::LodLevel;
using IRRender::LodZoomProfile;

// LuaScript first so its sol::state outlives the managers.
class LodZoomProfileLua : public testing::Test {
  protected:
    LodZoomProfileLua()
        : m_lua{}
        , m_entityManager{}
        , m_systemManager{} {
        m_lua.bindLuaDrivenEcs();
        m_lua.registerTypeFromTraits<C_LodZoomProfile>();
        m_camera = IREntity::createEntity();
        IREntity::setName(m_camera, "camera");
        m_lua.lua()["cameraEntity"] = [this]() { return IRScript::LuaEntity{m_camera}; };
    }

    void runLua(const std::string &source) {
        auto result = m_lua.lua().safe_script(source, sol::script_pass_on_error);
        ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    }

    // The error message of a script that must fail, or nullopt if it ran.
    std::optional<std::string> failLua(const std::string &source) {
        auto result = m_lua.lua().safe_script(source, sol::script_pass_on_error);
        if (result.valid()) {
            return std::nullopt;
        }
        return std::string{sol::error{result}.what()};
    }

    std::optional<LodZoomProfile> cameraProfile() const {
        const auto found = IREntity::getComponentOptional<C_LodZoomProfile>(m_camera);
        return found.has_value() ? std::optional{(*found)->profile_} : std::nullopt;
    }

    static LodLevel write(float zoom) {
        IRSystem::System<IRSystem::LOD_UPDATE>::writeActiveTier(IRMath::vec2(zoom));
        return IRRender::getActiveLodLevel();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    IREntity::EntityId m_camera = IREntity::kNullEntity;
};

TEST_F(LodZoomProfileLua, AttachSetsAndReadsTheProfile) {
    runLua(
        "IREntity.addLuaComponent(cameraEntity(), IRComponent.C_LodZoomProfile, "
        "{ thresholds = { 3, 6, 12, 24 }, hysteresis = 0.25 })"
    );
    const std::optional<LodZoomProfile> profile = cameraProfile();
    ASSERT_TRUE(profile.has_value());
    EXPECT_EQ(profile->thresholds(), (LodZoomProfile::Thresholds{3.0f, 6.0f, 12.0f, 24.0f}));
    EXPECT_EQ(profile->hysteresisOctaves(), 0.25f);

    // The profile drives the main camera's tier.
    EXPECT_EQ(write(1.0f), LodLevel::LOD_4);
    EXPECT_EQ(write(3.1f), LodLevel::LOD_4);
    EXPECT_EQ(write(3.6f), LodLevel::LOD_3);

    m_lua.lua()["heldProfile"] =
        std::ref(*IREntity::getComponentOptional<C_LodZoomProfile>(m_camera).value());
    runLua(
        "assert(heldProfile:threshold(1) == 3 and heldProfile:threshold(4) == 24)\n"
        "assert(heldProfile.hysteresis == 0.25)\n"
        "heldProfile:set{ hysteresis = 0 }\n"
        "assert(heldProfile.hysteresis == 0 and heldProfile:threshold(2) == 6)\n"
        "heldProfile:set{ thresholds = { 2, 4, 8, 16 } }\n"
        "assert(heldProfile:threshold(3) == 8)\n"
        "heldProfile = nil"
    );
    EXPECT_EQ(cameraProfile(), LodZoomProfile{});

    // An empty attach is the default profile.
    runLua("IREntity.addLuaComponent(cameraEntity(), IRComponent.C_LodZoomProfile, {})");
    EXPECT_EQ(cameraProfile(), LodZoomProfile{});
}

TEST_F(LodZoomProfileLua, InvalidInputsRaiseAndKeepThePreviousProfile) {
    runLua(
        "IREntity.addLuaComponent(cameraEntity(), IRComponent.C_LodZoomProfile, "
        "{ thresholds = { 3, 6, 12, 24 }, hysteresis = 0.25 })"
    );
    const std::optional<LodZoomProfile> before = cameraProfile();
    ASSERT_TRUE(before.has_value());

    const char *const invalidFields[] = {
        "{ thresholds = { 3, 6, 5, 24 } }",
        "{ thresholds = { 0, 6, 12, 24 } }",
        "{ thresholds = { -3, 6, 12, 24 } }",
        "{ thresholds = { 3, 6, 12, 1/0 } }",
        "{ thresholds = { 3, 6, 12, 0/0 } }",
        "{ thresholds = { 3, 6, 12 } }",
        "{ thresholds = { 3, 6, 12, 24, 48 } }",
        "{ thresholds = { 3, 6, 'x', 24 } }",
        "{ thresholds = 4 }",
        "{ hysteresis = -0.5 }",
        "{ hysteresis = 0/0 }",
        "{ hysteresis = 'wide' }",
        "{ hysteresis = 0.75 }",
    };
    m_lua.lua()["heldProfile"] =
        std::ref(*IREntity::getComponentOptional<C_LodZoomProfile>(m_camera).value());
    for (const char *fields : invalidFields) {
        const std::optional<std::string> attach = failLua(
            std::string{"IREntity.addLuaComponent(cameraEntity(), IRComponent.C_LodZoomProfile, "} +
            fields + ")"
        );
        ASSERT_TRUE(attach.has_value()) << fields;
        EXPECT_NE(attach->find("C_LodZoomProfile"), std::string::npos) << *attach;
        EXPECT_EQ(cameraProfile(), before) << fields;

        const std::optional<std::string> set = failLua(std::string{"heldProfile:set"} + fields);
        ASSERT_TRUE(set.has_value()) << fields;
        EXPECT_EQ(cameraProfile(), before) << fields;
    }
    EXPECT_TRUE(failLua("return heldProfile:threshold(0)").has_value());
    EXPECT_TRUE(failLua("return heldProfile:threshold(5)").has_value());
    runLua("heldProfile = nil");

    // The same rule names the failure on both sides of the seam.
    const std::optional<std::string> overlap = failLua(
        "IREntity.addLuaComponent(cameraEntity(), IRComponent.C_LodZoomProfile, "
        "{ thresholds = { 3, 6, 12, 24 }, hysteresis = 0.75 })"
    );
    ASSERT_TRUE(overlap.has_value());
    const auto why = LodZoomProfile::whyInvalid({3.0f, 6.0f, 12.0f, 24.0f}, 0.75f);
    ASSERT_TRUE(why.has_value());
    EXPECT_NE(overlap->find(std::string{*why}), std::string::npos) << *overlap;
}

TEST_F(LodZoomProfileLua, APrefabDeclarationAttachesTheProfile) {
    const std::string path = "/tmp/lod_zoom_profile_lua_test.prefab.lua";
    {
        std::ofstream out(path);
        out << "return {\n"
               "  prefab_version = 1,\n"
               "  components = { C_LodZoomProfile = { thresholds = { 3, 6, 12, 24 }, "
               "hysteresis = 0.125 } },\n"
               "}\n";
    }
    IRPrefab::Prefab::registerPrefab("lod_zoom_profile", path);
    const IRPrefab::Prefab::SpawnResult spawned =
        IRPrefab::Prefab::spawnPrefab(m_lua, "lod_zoom_profile", IRMath::vec3(0.0f));
    IRPrefab::Prefab::clearPrefabs();
    ASSERT_NE(spawned.entity_, IREntity::kNullEntity) << spawned.error_;
    const auto profile = IREntity::getComponentOptional<C_LodZoomProfile>(spawned.entity_);
    ASSERT_TRUE(profile.has_value());
    EXPECT_EQ((*profile)->profile_.thresholds()[0], 3.0f);
    EXPECT_EQ((*profile)->profile_.hysteresisOctaves(), 0.125f);
}

} // namespace
