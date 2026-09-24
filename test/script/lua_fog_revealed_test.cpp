#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/render/active_canvas.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_fog_exempt.hpp>
#include <irreden/render/components/component_fog_exempt_lua.hpp>
#include <irreden/render/components/component_fog_field.hpp>
#include <irreden/render/components/component_fog_field_lua.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/components/component_fog_revealed_lua.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/fog_reveal_systems.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_set_lua.hpp>

#include <cstdint>

namespace {

class LuaFogRevealedTest : public testing::Test {
  protected:
    LuaFogRevealedTest()
        : m_lua{}
        , m_entityManager{}
        , m_systemManager{} {
        m_lua.bindLuaDrivenEcs();
        m_lua.registerTypesFromTraits<
            IRComponents::C_FogRevealed,
            IRComponents::C_FogField,
            IRComponents::C_FogExempt>();
        m_lua.registerCreateEntityFunction<IRComponents::C_FogField>("createFieldEntity");
        m_lua.registerCreateEntityFunction<IRComponents::C_FogExempt>("createExemptEntity");
        m_lua.registerCreateEntityFunction<>("createPlainEntity");
    }
    ~LuaFogRevealedTest() override {
        IRRender::setHeadlessActiveCanvasEntity(IREntity::kNullEntity);
    }

    static bool hasRevealed(IREntity::EntityId entity) {
        return IREntity::getComponentOptional<IRComponents::C_FogRevealed>(entity).has_value();
    }

    // The set's carrier when every record carries the same one, else ~0u.
    static std::uint32_t uniformCarrierOf(IREntity::EntityId entity) {
        const auto &set = IREntity::getComponent<IRComponents::C_VoxelSetNew>(entity);
        auto &pool = IREntity::getComponent<IRComponents::C_VoxelPool>(set.canvasEntity_);
        const auto records = IRPrefab::Fog::poolRecords(pool, set);
        const std::uint32_t carrier =
            records[0].reserved_ & IRComponents::VoxelReserved::kFogCarrierMask;
        for (const IRComponents::C_Voxel &voxel : records) {
            if ((voxel.reserved_ & IRComponents::VoxelReserved::kFogCarrierMask) != carrier) {
                return ~0u;
            }
        }
        return carrier;
    }

    static int activeBitsOf(IREntity::EntityId entity) {
        const auto &set = IREntity::getComponent<IRComponents::C_VoxelSetNew>(entity);
        const auto &mask =
            IREntity::getComponent<IRComponents::C_VoxelPool>(set.canvasEntity_).getActiveMask();
        int active = 0;
        for (int i = 0; i < set.numVoxels_; ++i) {
            const std::size_t idx = set.voxelStartIdx_ + static_cast<std::size_t>(i);
            active += (mask[idx / 32] >> (idx % 32)) & 1u;
        }
        return active;
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
};

TEST_F(LuaFogRevealedTest, ArchetypeColumnReadsCppRevealFactor) {
    IREntity::createEntity(IRComponents::C_FogRevealed{0.625f, true});
    auto result = m_lua.lua().safe_script(
        R"lua(
        observedReveal = nil
        return IRSystem.registerSystem({
            name = 'ReadFogReveal',
            components = { IRComponent.C_FogRevealed },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    observedReveal = arch.C_FogRevealed:at(i).revealFactor
                end
            end,
        })
    )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    const IRSystem::SystemId systemId = result.get<lua_Integer>();
    m_systemManager.registerPipeline(IRTime::Events::UPDATE, {systemId});
    m_systemManager.executePipeline(IRTime::Events::UPDATE);
    EXPECT_FLOAT_EQ(m_lua.lua()["observedReveal"].get<float>(), 0.625f);
}

// The marker components are Lua-constructible, and an entity spawned from
// Lua with one reads that class through the C++ surface; a marker-less
// entity reads the BODY default.
TEST_F(LuaFogRevealedTest, MarkerAttachedFromLuaReadsItsSubjectClass) {
    auto result = m_lua.lua().safe_script(
        R"lua(
        local field = IREntity.createFieldEntity(C_FogField.new())
        local exempt = IREntity.createExemptEntity(C_FogExempt.new())
        local plain = IREntity.createPlainEntity()
        return field.entity, exempt.entity, plain.entity
    )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    auto [field, exempt, plain] = result.get<std::tuple<lua_Integer, lua_Integer, lua_Integer>>();
    EXPECT_EQ(
        IRPrefab::Fog::subjectClass(static_cast<IREntity::EntityId>(field)),
        IRPrefab::Fog::FogSubjectClass::FIELD
    );
    EXPECT_EQ(
        IRPrefab::Fog::subjectClass(static_cast<IREntity::EntityId>(exempt)),
        IRPrefab::Fog::FogSubjectClass::EXEMPT
    );
    EXPECT_EQ(
        IRPrefab::Fog::subjectClass(static_cast<IREntity::EntityId>(plain)),
        IRPrefab::Fog::FogSubjectClass::BODY
    );
}

// The marker's Lua route end to end: a voxel set spawned from Lua with
// `C_FogExempt` on a fogged canvas is realized by FOG_SUBJECT_EXEMPT at factor
// 255 and never adopted, while its marker-less twin on the same unexplored cell
// is adopted as a hidden BODY.
TEST_F(LuaFogRevealedTest, MarkerAttachedFromLuaIsRealizedAsExemptAndItsTwinIsAdopted) {
    using IRComponents::C_FogExempt;
    using IRComponents::C_VoxelSetNew;
    const IREntity::EntityId canvas = IREntity::createEntity(
        IRComponents::C_VoxelPool{IRMath::ivec3(8, 8, 8)},
        IRComponents::C_CanvasFogOfWar{IRComponents::C_CanvasFogOfWar::HeadlessInit{}}
    );
    IRRender::setHeadlessActiveCanvasEntity(canvas);
    const IRSystem::SystemId exemptId = IRSystem::createSystem<IRSystem::FOG_SUBJECT_EXEMPT>();
    const IRSystem::SystemId adoptId = IRSystem::createSystem<IRSystem::FOG_SUBJECT_ADOPT>();
    m_systemManager.registerPipeline(IRTime::Events::UPDATE, {exemptId, adoptId});

    m_lua.registerType<IRMath::Color, IRMath::Color(int, int, int, int)>("Color");
    m_lua.registerType<IRMath::ivec3, IRMath::ivec3(int, int, int)>("ivec3");
    m_lua.registerTypeFromTraits<C_VoxelSetNew>();
    m_lua.registerCreateEntityFunction<C_VoxelSetNew, C_FogExempt>("createExemptSet");
    m_lua.registerCreateEntityFunction<C_VoxelSetNew>("createPlainSet");
    m_lua.lua()["fogCanvas"] = canvas;
    auto result = m_lua.lua().safe_script(
        R"lua(
        local function newSet()
            return C_VoxelSetNew.new(
                ivec3.new(2, 2, 2),
                Color.new(200, 100, 50, 255),
                IRComponent.EntityAnchor.CORNER,
                fogCanvas
            )
        end
        local exempt = IREntity.createExemptSet(newSet(), C_FogExempt.new())
        local twin = IREntity.createPlainSet(newSet())
        return exempt.entity, twin.entity
    )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    auto [exemptRaw, twinRaw] = result.get<std::tuple<lua_Integer, lua_Integer>>();
    const auto exempt = static_cast<IREntity::EntityId>(exemptRaw);
    const auto twin = static_cast<IREntity::EntityId>(twinRaw);
    const int setVoxels = IREntity::getComponent<C_VoxelSetNew>(exempt).numVoxels_;
    ASSERT_GT(setVoxels, 0);

    m_systemManager.executePipeline(IRTime::Events::UPDATE);
    IREntity::flushStructuralChanges();

    const std::uint32_t exemptCarrier = IRComponents::VoxelReserved::kFogBody |
                                        (0xFFu << IRComponents::VoxelReserved::kFogBodyFactorShift);
    EXPECT_EQ(uniformCarrierOf(exempt), exemptCarrier);
    EXPECT_FALSE(hasRevealed(exempt)) << "an EXEMPT set carries no BODY state";
    EXPECT_EQ(activeBitsOf(exempt), setVoxels) << "an EXEMPT set is never hidden";
    EXPECT_EQ(IRPrefab::Fog::subjectClass(exempt), IRPrefab::Fog::FogSubjectClass::EXEMPT);

    EXPECT_TRUE(hasRevealed(twin)) << "the marker-less twin is adopted";
    EXPECT_EQ(uniformCarrierOf(twin), IRComponents::VoxelReserved::kFogBody)
        << "the twin is a BODY at factor 0 on the unexplored cell";
    EXPECT_EQ(activeBitsOf(twin), 0) << "the twin's hidden verdict clears its mask";
}

} // namespace
