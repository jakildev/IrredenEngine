#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/script/lua_script.hpp>

#include <string>
#include <tuple>
#include <utility>

namespace {

template <int Index> struct BatchComponent {
    BatchComponent() = default;
    explicit BatchComponent(float value)
        : value_{value} {}

    float value_ = 0.0f;
};

class LuaEntityBatchCreateTest : public testing::Test {
  protected:
    LuaEntityBatchCreateTest()
        : m_lua{}
        , m_entity_manager{} {
        m_lua.registerType<IRMath::ivec3, IRMath::ivec3(int, int, int)>("ivec3");
        m_lua.registerType<IRScript::LuaEntity, IRScript::LuaEntity(IREntity::EntityId)>(
            "LuaEntity",
            "entity",
            &IRScript::LuaEntity::entity
        );
        registerComponents(std::make_integer_sequence<int, 13>{});
        m_lua.registerCreateEntityBatchFunction<
            BatchComponent<1>,
            BatchComponent<2>,
            BatchComponent<3>,
            BatchComponent<4>,
            BatchComponent<5>,
            BatchComponent<6>,
            BatchComponent<7>,
            BatchComponent<8>,
            BatchComponent<9>,
            BatchComponent<10>,
            BatchComponent<11>,
            BatchComponent<12>,
            BatchComponent<13>>("createBatch13");
    }

    template <int Index> void registerComponent() {
        const std::string name = "BatchComponent" + std::to_string(Index);
        m_lua.registerType<
            BatchComponent<Index>,
            BatchComponent<Index>(),
            BatchComponent<Index>(float)>(name, "value", &BatchComponent<Index>::value_);
    }

    template <int... Indices> void registerComponents(std::integer_sequence<int, Indices...>) {
        (registerComponent<Indices + 1>(), ...);
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
};

TEST_F(LuaEntityBatchCreateTest, CreatesThirteenComponentRowsFromLuaFactories) {
    auto result = m_lua.lua().safe_script(
        R"lua(
        local rows = IREntity.createBatch13(
            ivec3.new(2, 1, 1),
            function() return BatchComponent1.new(1) end,
            function() return BatchComponent2.new(2) end,
            function() return BatchComponent3.new(3) end,
            function() return BatchComponent4.new(4) end,
            function() return BatchComponent5.new(5) end,
            function() return BatchComponent6.new(6) end,
            function() return BatchComponent7.new(7) end,
            function() return BatchComponent8.new(8) end,
            function() return BatchComponent9.new(9) end,
            function() return BatchComponent10.new(10) end,
            function() return BatchComponent11.new(11) end,
            function() return BatchComponent12.new(12) end,
            function() return BatchComponent13.new(13) end
        )
        return #rows, rows[1].entity, rows[2].entity
    )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error{result}.what();

    const auto [rowCount, first, second] =
        result.get<std::tuple<int, IREntity::EntityId, IREntity::EntityId>>();
    ASSERT_EQ(rowCount, 2);
    EXPECT_FLOAT_EQ(IREntity::getComponent<BatchComponent<1>>(first).value_, 1.0f);
    EXPECT_FLOAT_EQ(IREntity::getComponent<BatchComponent<13>>(first).value_, 13.0f);
    EXPECT_FLOAT_EQ(IREntity::getComponent<BatchComponent<1>>(second).value_, 1.0f);
    EXPECT_FLOAT_EQ(IREntity::getComponent<BatchComponent<13>>(second).value_, 13.0f);
}

} // namespace
