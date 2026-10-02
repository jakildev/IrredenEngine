#ifndef LUA_FOG_BINDINGS_H
#define LUA_FOG_BINDINGS_H

#include <irreden/render/fog_of_war.hpp>
#include <irreden/script/ir_script_types.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
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

inline double requireFogNumber(sol::object value, const std::string &name) {
    if (value.get_type() != sol::type::number) {
        throw std::invalid_argument(name + " must be a number");
    }
    const double number = value.as<double>();
    const double maximum = static_cast<double>(std::numeric_limits<float>::max());
    if (!(number >= -maximum && number <= maximum)) {
        throw std::invalid_argument(name + " must be finite");
    }
    return number;
}

inline double requireFogNumber(sol::object value, const char *function, std::size_t index) {
    return requireFogNumber(value, fogArgumentName(function, index));
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

inline std::uint32_t requireFogUint32(sol::object value, const char *function, std::size_t index) {
    const double number = requireFogNumber(value, function, index);
    constexpr double maximum = static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    if (!(number >= 0.0 && number <= maximum)) {
        throw std::invalid_argument(fogArgumentName(function, index) + " is outside uint32 range");
    }
    const auto result = static_cast<std::uint32_t>(number);
    if (static_cast<double>(result) != number) {
        throw std::invalid_argument(fogArgumentName(function, index) + " must be an integer");
    }
    return result;
}

inline std::uint32_t optionalFogUint32(
    const sol::variadic_args &args, std::size_t index, std::uint32_t fallback, const char *function
) {
    if (index >= args.size() || args[index].get_type() == sol::type::lua_nil) {
        return fallback;
    }
    return requireFogUint32(args[index], function, index);
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

/// The vision state the `IRFog` vision entries author: the analytic slots and
/// the world field whose transient layer takes every source past the cap.
/// Both are set, or both null (no canvas owns fog; the entries then no-op).
struct FogVisionTarget {
    IRComponents::FrameDataFogObservers *observers_ = nullptr;
    IRPrefab::Fog::WorldField *field_ = nullptr;
};

using FogVisionTargetResolver = std::function<FogVisionTarget()>;

inline FogVisionTarget activeFogVisionTarget() {
    if (auto *fog = IRPrefab::Fog::detail::activeFogComponent()) {
        return {&fog->observers_, fog->field_.get()};
    }
    return {};
}

inline int
applyFogVision(const sol::variadic_args &args, bool replace, const FogVisionTarget &target) {
    const char *function = replace ? "setVision" : "addVision";
    requireFogArity(function, args.size(), 3, 9);
    const float cx = requireFogFloat(args[0], function, 0);
    const float cy = requireFogFloat(args[1], function, 1);
    const float radius = requireFogFloat(args[2], function, 2);
    const float edge = optionalFogFloat(args, 3, IRComponents::kFogVisionEdgeDefault, function);
    const float observerZ = optionalFogFloat(args, 4, 0.0f, function);
    const float zCostUp = optionalFogFloat(args, 5, 0.0f, function);
    const float zCostDown =
        optionalFogFloat(args, 6, IRComponents::kFogVisionZCostMirrorUp, function);
    const float freeBand = optionalFogFloat(args, 7, 0.0f, function);
    const std::uint32_t channels =
        optionalFogUint32(args, 8, IRComponents::kFogChannelDefault, function);
    if (target.observers_ == nullptr) {
        return -1;
    }
    if (replace) {
        IRComponents::C_CanvasFogOfWar::clearVisionCircles(*target.observers_, *target.field_);
    }
    return IRComponents::C_CanvasFogOfWar::addVisionCircle(
        *target.observers_,
        *target.field_,
        cx,
        cy,
        radius,
        edge,
        observerZ,
        zCostUp,
        zCostDown,
        freeBand,
        channels
    );
}

/// `IRFog.setVisionLineOfSight(slot, losEyeHeight[, losSoftness])`: validates
/// in Lua what `C_CanvasFogOfWar::setVisionCircleLineOfSight` asserts, so an
/// unregistered slot raises a named error instead of reaching the assert.
inline void applyFogVisionLineOfSight(
    const sol::variadic_args &args, IRComponents::FrameDataFogObservers *observers
) {
    constexpr const char *function = "setVisionLineOfSight";
    requireFogArity(function, args.size(), 2, 3);
    const int slot = requireFogInt(args[0], function, 0);
    const float eyeHeight = requireFogFloat(args[1], function, 1);
    const float softness = optionalFogFloat(args, 2, IRComponents::kFogLosHardGate, function);
    if (observers == nullptr) {
        return;
    }
    if (slot < 0 || slot >= observers->visionCircleCount_) {
        throw std::invalid_argument(
            fogArgumentName(function, 0) + " is not a registered vision slot (count " +
            std::to_string(observers->visionCircleCount_) + ")"
        );
    }
    IRComponents::C_CanvasFogOfWar::setVisionCircleLineOfSight(
        *observers,
        slot,
        eyeHeight,
        softness
    );
}

/// `IRFog.lineOfSightCaptured(fx, fy, fz, targets)`: one verdict per target of
/// the flat `{tx1, ty1, tz1, tx2, ...}` array from the eye, over @p view.
inline sol::table queryFogLineOfSightCaptured(
    sol::this_state state,
    const sol::variadic_args &args,
    const IRPrefab::Fog::LineOfSightView &view
) {
    constexpr const char *function = "lineOfSightCaptured";
    requireFogArity(function, args.size(), 4, 4);
    const IRMath::vec3 eye(
        requireFogFloat(args[0], function, 0),
        requireFogFloat(args[1], function, 1),
        requireFogFloat(args[2], function, 2)
    );
    sol::object targetsObject = args[3];
    if (targetsObject.get_type() != sol::type::table) {
        throw std::invalid_argument(fogArgumentName(function, 3) + " must be a table");
    }
    const sol::table targets = targetsObject.as<sol::table>();
    const std::size_t length = targets.size();
    if (length % 3 != 0) {
        throw std::invalid_argument(
            fogArgumentName(function, 3) + " length must be a multiple of 3, got " +
            std::to_string(length)
        );
    }
    sol::table verdicts = sol::state_view(state).create_table(static_cast<int>(length / 3), 0);
    for (std::size_t element = 1; element <= length; element += 3) {
        float target[3];
        for (std::size_t axis = 0; axis < 3; ++axis) {
            target[axis] = static_cast<float>(requireFogNumber(
                targets.raw_get<sol::object>(element + axis),
                fogArgumentName(function, 3) + " element " + std::to_string(element + axis)
            ));
        }
        verdicts.raw_set(
            element / 3 + 1,
            view.visible(eye, IRMath::vec3(target[0], target[1], target[2]))
        );
    }
    return verdicts;
}

/// @p resolveTarget names the vision state the vision entries author; the
/// default is the active canvas's.
inline void
bindFog(LuaScript &script, FogVisionTargetResolver resolveTarget = activeFogVisionTarget) {
    sol::state &lua = script.lua();
    sol::object existing = lua["IRFog"];
    if (existing.valid() && existing.get_type() != sol::type::lua_nil &&
        existing.get_type() != sol::type::table) {
        throw std::invalid_argument("IRFog must be a table before bindLuaFog()");
    }

    sol::table fog =
        existing.get_type() == sol::type::table ? existing.as<sol::table>() : lua.create_table();

    fog["setVision"] = [resolveTarget](sol::variadic_args args) {
        return applyFogVision(args, true, resolveTarget());
    };
    fog["addVision"] = [resolveTarget](sol::variadic_args args) {
        return applyFogVision(args, false, resolveTarget());
    };
    fog["setVisionLineOfSight"] = [resolveTarget](sol::variadic_args args) {
        applyFogVisionLineOfSight(args, resolveTarget().observers_);
    };
    fog["clearVisions"] = [resolveTarget](sol::variadic_args args) {
        requireFogArity("clearVisions", args.size(), 0, 0);
        const FogVisionTarget target = resolveTarget();
        if (target.observers_ != nullptr) {
            IRComponents::C_CanvasFogOfWar::clearVisionCircles(*target.observers_, *target.field_);
        }
    };
    fog["evalReveal"] = [](sol::variadic_args args) {
        requireFogArity("evalReveal", args.size(), 3, 4);
        const std::uint32_t channels =
            optionalFogUint32(args, 3, IRComponents::kFogChannelDefault, "evalReveal");
        return IRPrefab::Fog::evalActiveReveal(
            IRMath::vec3(
                requireFogFloat(args[0], "evalReveal", 0),
                requireFogFloat(args[1], "evalReveal", 1),
                requireFogFloat(args[2], "evalReveal", 2)
            ),
            channels
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
    auto lineOfSightView = std::make_shared<IRPrefab::Fog::LineOfSightView>();
    fog["captureLineOfSight"] = [lineOfSightView](sol::variadic_args args) {
        requireFogArity("captureLineOfSight", args.size(), 0, 0);
        IRPrefab::Fog::captureLineOfSight(*lineOfSightView);
    };
    fog["lineOfSightCaptured"] = [lineOfSightView](sol::this_state state, sol::variadic_args args) {
        return queryFogLineOfSightCaptured(state, args, *lineOfSightView);
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
        // BODY is the class an untagged entity already reads as, so the
        // adopted state is the "already" test for it; FIELD is its marker.
        const bool alreadyTarget =
            governed
                ? IREntity::getComponentOptional<IRComponents::C_FogRevealed>(entity).has_value()
                : IRPrefab::Fog::subjectClass(entity) == IRPrefab::Fog::FogSubjectClass::FIELD;
        if (alreadyTarget) {
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
