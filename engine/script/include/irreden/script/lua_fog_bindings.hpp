#ifndef LUA_FOG_BINDINGS_H
#define LUA_FOG_BINDINGS_H

#include <irreden/render/fog_of_war.hpp>
#include <irreden/script/ir_script_types.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace IRScript::detail {

inline std::string fogArgumentName(const char *function, std::size_t index) {
    return std::string("IRFog.") + function + " argument " + std::to_string(index + 1);
}

inline void requireFogArity(
    const char *function, std::size_t actual, std::size_t minimum, std::size_t maximum
) {
    if (actual < minimum || actual > maximum) {
        throw std::invalid_argument(
            std::string("IRFog.") + function + " expects " +
            (minimum == maximum ? std::to_string(minimum)
                                : std::to_string(minimum) + " to " + std::to_string(maximum)) +
            " arguments, got " + std::to_string(actual)
        );
    }
}

inline double requireFogNumber(sol::object value, const char *function, std::size_t index) {
    if (value.get_type() != sol::type::number) {
        throw std::invalid_argument(fogArgumentName(function, index) + " must be a number");
    }
    const double number = value.as<double>();
    const double maximum = static_cast<double>(std::numeric_limits<float>::max());
    if (!(number >= -maximum && number <= maximum)) {
        throw std::invalid_argument(fogArgumentName(function, index) + " must be finite");
    }
    return number;
}

inline float requireFogFloat(sol::object value, const char *function, std::size_t index) {
    return static_cast<float>(requireFogNumber(value, function, index));
}

inline float optionalFogFloat(
    const sol::variadic_args &args, std::size_t index, float fallback, const char *function
) {
    if (index >= args.size()) {
        return fallback;
    }
    sol::object value = args[index];
    if (value.get_type() == sol::type::lua_nil) {
        return fallback;
    }
    return requireFogFloat(value, function, index);
}

inline int requireFogInt(sol::object value, const char *function, std::size_t index) {
    const double number = requireFogNumber(value, function, index);
    constexpr double minimum = static_cast<double>(std::numeric_limits<int>::min());
    constexpr double maximum = static_cast<double>(std::numeric_limits<int>::max());
    if (!(number >= minimum && number <= maximum)) {
        throw std::invalid_argument(fogArgumentName(function, index) + " is outside int range");
    }
    const int result = static_cast<int>(number);
    if (static_cast<double>(result) != number) {
        throw std::invalid_argument(fogArgumentName(function, index) + " must be an integer");
    }
    return result;
}

inline IREntity::EntityId
requireFogEntity(sol::object value, const char *function, std::size_t index) {
    IREntity::EntityId entity = IREntity::kNullEntity;
    if (value.is<IRScript::LuaEntity>()) {
        entity = value.as<IRScript::LuaEntity>().entity;
    } else {
        const double number = requireFogNumber(value, function, index);
        constexpr double kLargestExactLuaInteger = 9007199254740991.0;
        if (!(number >= 0.0 && number <= kLargestExactLuaInteger)) {
            throw std::invalid_argument(
                fogArgumentName(function, index) + " is outside the exact entity-id range"
            );
        }
        entity = static_cast<IREntity::EntityId>(number);
        if (static_cast<double>(entity) != number) {
            throw std::invalid_argument(
                fogArgumentName(function, index) + " must be an integer entity id"
            );
        }
    }
    if (entity == IREntity::kNullEntity || !IREntity::entityExists(entity)) {
        throw std::invalid_argument(fogArgumentName(function, index) + " is not a live entity");
    }
    return entity;
}

inline void applyFogVision(const sol::variadic_args &args, bool replace) {
    const char *function = replace ? "setVision" : "addVision";
    requireFogArity(function, args.size(), 3, 8);
    const float cx = requireFogFloat(args[0], function, 0);
    const float cy = requireFogFloat(args[1], function, 1);
    const float radius = requireFogFloat(args[2], function, 2);
    const float edge = optionalFogFloat(args, 3, IRComponents::kFogVisionEdgeDefault, function);
    const float observerZ = optionalFogFloat(args, 4, 0.0f, function);
    const float zCostUp = optionalFogFloat(args, 5, 0.0f, function);
    const float zCostDown =
        optionalFogFloat(args, 6, IRComponents::kFogVisionZCostMirrorUp, function);
    const float freeBand = optionalFogFloat(args, 7, 0.0f, function);
    if (replace) {
        IRPrefab::Fog::setVisionCircle(
            cx,
            cy,
            radius,
            edge,
            observerZ,
            zCostUp,
            zCostDown,
            freeBand
        );
        return;
    }
    IRPrefab::Fog::addVisionCircle(cx, cy, radius, edge, observerZ, zCostUp, zCostDown, freeBand);
}

inline void bindFog(LuaScript &script) {
    sol::state &lua = script.lua();
    sol::object existing = lua["IRFog"];
    if (existing.valid() && existing.get_type() != sol::type::lua_nil &&
        existing.get_type() != sol::type::table) {
        throw std::invalid_argument("IRFog must be a table before bindLuaFog()");
    }

    sol::table fog =
        existing.get_type() == sol::type::table ? existing.as<sol::table>() : lua.create_table();

    fog["setVision"] = [](sol::variadic_args args) { applyFogVision(args, true); };
    fog["addVision"] = [](sol::variadic_args args) { applyFogVision(args, false); };
    fog["clearVisions"] = [](sol::variadic_args args) {
        requireFogArity("clearVisions", args.size(), 0, 0);
        IRPrefab::Fog::clearVisionCircles();
    };
    fog["evalReveal"] = [](sol::variadic_args args) {
        requireFogArity("evalReveal", args.size(), 3, 3);
        return IRPrefab::Fog::evalActiveVisionReveal(
            IRMath::vec3(
                requireFogFloat(args[0], "evalReveal", 0),
                requireFogFloat(args[1], "evalReveal", 1),
                requireFogFloat(args[2], "evalReveal", 2)
            )
        );
    };
    fog["lineOfSight"] = [](sol::variadic_args args) {
        requireFogArity("lineOfSight", args.size(), 6, 6);
        float coordinates[6];
        for (std::size_t index = 0; index < 6; ++index) {
            coordinates[index] = requireFogFloat(args[index], "lineOfSight", index);
        }
        return IRPrefab::Fog::lineOfSight(
            IRMath::vec3(coordinates[0], coordinates[1], coordinates[2]),
            IRMath::vec3(coordinates[3], coordinates[4], coordinates[5])
        );
    };
    fog["setEntityGoverned"] = [](sol::variadic_args args) {
        requireFogArity("setEntityGoverned", args.size(), 1, 2);
        const IREntity::EntityId entity = requireFogEntity(args[0], "setEntityGoverned", 0);
        bool governed = true;
        if (args.size() == 2 && args[1].get_type() != sol::type::lua_nil) {
            if (args[1].get_type() != sol::type::boolean) {
                throw std::invalid_argument("IRFog.setEntityGoverned argument 2 must be a boolean");
            }
            governed = args[1].as<bool>();
        }
        if (!IRPrefab::Fog::entityRevealGovernanceSupportsActiveCanvas(entity)) {
            throw std::invalid_argument(
                "IRFog.setEntityGoverned supports only the active grid canvas"
            );
        }
        const bool alreadyGoverned =
            IREntity::getComponentOptional<IRComponents::C_FogRevealed>(entity).has_value();
        if (alreadyGoverned == governed) {
            return;
        }
        IRPrefab::Fog::setEntityRevealGoverned(entity, governed);
    };
    fog["getEntityReveal"] = [](sol::variadic_args args) {
        requireFogArity("getEntityReveal", args.size(), 1, 1);
        return IRPrefab::Fog::getEntityReveal(requireFogEntity(args[0], "getEntityReveal", 0));
    };
    fog["setCell"] = [](sol::variadic_args args) {
        requireFogArity("setCell", args.size(), 3, 3);
        const int x = requireFogInt(args[0], "setCell", 0);
        const int y = requireFogInt(args[1], "setCell", 1);
        const int state = requireFogInt(args[2], "setCell", 2);
        if (state != IRComponents::kFogStateUnexplored &&
            state != IRComponents::kFogStateExplored && state != IRComponents::kFogStateVisible) {
            throw std::invalid_argument("IRFog.setCell argument 3 must be an IRFog.State value");
        }
        IRPrefab::Fog::setCell(x, y, static_cast<std::uint8_t>(state));
    };
    fog["getCell"] = [](sol::variadic_args args) {
        requireFogArity("getCell", args.size(), 2, 2);
        return static_cast<lua_Integer>(IRPrefab::Fog::getCell(
            requireFogInt(args[0], "getCell", 0),
            requireFogInt(args[1], "getCell", 1)
        ));
    };
    fog["revealRadius"] = [](sol::variadic_args args) {
        requireFogArity("revealRadius", args.size(), 3, 3);
        const int cx = requireFogInt(args[0], "revealRadius", 0);
        const int cy = requireFogInt(args[1], "revealRadius", 1);
        const int radius = requireFogInt(args[2], "revealRadius", 2);
        const std::int64_t minX = static_cast<std::int64_t>(cx) - radius;
        const std::int64_t maxX = static_cast<std::int64_t>(cx) + radius;
        const std::int64_t minY = static_cast<std::int64_t>(cy) - radius;
        const std::int64_t maxY = static_cast<std::int64_t>(cy) + radius;
        if (minX < std::numeric_limits<int>::min() || maxX > std::numeric_limits<int>::max() ||
            minY < std::numeric_limits<int>::min() || maxY > std::numeric_limits<int>::max()) {
            throw std::invalid_argument("IRFog.revealRadius arguments overflow integer bounds");
        }
        IRPrefab::Fog::revealRadius(cx, cy, radius);
    };
    fog["clear"] = [](sol::variadic_args args) {
        requireFogArity("clear", args.size(), 0, 0);
        IRPrefab::Fog::clear();
    };

    sol::table state = lua.create_table();
#define IR_BIND_FOG_STATE(name, value) state[#name] = static_cast<lua_Integer>(value)
    IR_BIND_FOG_STATE(UNEXPLORED, IRComponents::kFogStateUnexplored);
    IR_BIND_FOG_STATE(EXPLORED, IRComponents::kFogStateExplored);
    IR_BIND_FOG_STATE(VISIBLE, IRComponents::kFogStateVisible);
#undef IR_BIND_FOG_STATE
    fog["State"] = state;
    lua["IRFog"] = fog;
}

} // namespace IRScript::detail

#endif /* LUA_FOG_BINDINGS_H */
