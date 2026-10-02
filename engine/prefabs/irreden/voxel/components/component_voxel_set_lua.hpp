#ifndef COMPONENT_VOXEL_SET_LUA_H
#define COMPONENT_VOXEL_SET_LUA_H

#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/dense_bridge.hpp>
#include <irreden/voxel/sdf_fill.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>

#include <memory>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_VoxelSetNew> = true;

namespace detail {

inline void requireVoxelCell(
    const IRComponents::C_VoxelSetNew &set, int x, int y, int z, const char *operation
) {
    if (x < 0 || x >= set.size_.x || y < 0 || y >= set.size_.y || z < 0 || z >= set.size_.z) {
        throw sol::error{
            std::string{operation} + ": voxel (" + std::to_string(x) + ", " + std::to_string(y) +
            ", " + std::to_string(z) + ") is outside set size (" + std::to_string(set.size_.x) +
            ", " + std::to_string(set.size_.y) + ", " + std::to_string(set.size_.z) + ")"
        };
    }
}

inline void
setVoxelRaw(IRComponents::C_VoxelSetNew &set, int x, int y, int z, IRMath::Color color) {
    requireVoxelCell(set, x, y, z, "setVoxel");
    auto records = IRPrefab::Voxel::detail::editableRecords(set);
    const std::size_t flat =
        static_cast<std::size_t>(IRMath::index3DtoIndex1D({x, y, z}, set.size_));
    IRPrefab::Voxel::detail::placeVoxel(records[flat], color);
}

inline void clearVoxelRaw(IRComponents::C_VoxelSetNew &set, int x, int y, int z) {
    requireVoxelCell(set, x, y, z, "clearVoxel");
    auto records = IRPrefab::Voxel::detail::editableRecords(set);
    const std::size_t flat =
        static_cast<std::size_t>(IRMath::index3DtoIndex1D({x, y, z}, set.size_));
    records[flat].deactivate();
}

struct VoxelBatchState {
    explicit VoxelBatchState(IRComponents::C_VoxelSetNew &set)
        : set_{&set} {}

    IRComponents::C_VoxelSetNew *set_;
};

inline IRComponents::C_VoxelSetNew &
requireActiveBatch(const std::shared_ptr<VoxelBatchState> &state) {
    if (state->set_ == nullptr) {
        throw sol::error{"batch handle used outside its callback"};
    }
    return *state->set_;
}

inline IRMath::SDF::ShapeType shapeTypeFromLua(lua_Integer value, const char *operation) {
    using IRMath::SDF::ShapeType;
    switch (static_cast<ShapeType>(value)) {
    case ShapeType::BOX:
    case ShapeType::SPHERE:
    case ShapeType::CYLINDER:
    case ShapeType::ELLIPSOID:
    case ShapeType::CURVED_PANEL:
    case ShapeType::WEDGE:
    case ShapeType::TAPERED_BOX:
    case ShapeType::CONE:
    case ShapeType::TORUS:
        return static_cast<ShapeType>(value);
    case ShapeType::CUSTOM_SDF:
        break;
    }
    throw sol::error{std::string{operation} + ": shape must be a supported IRShape value"};
}

inline void applySdfRaw(
    IRComponents::C_VoxelSetNew &set,
    lua_Integer shape,
    const sol::table &params,
    IRMath::Color color,
    bool place
) {
    auto records = IRPrefab::Voxel::detail::editableRecords(set);
    IRPrefab::Voxel::fillSdfRaw(
        set,
        shapeTypeFromLua(shape, place ? "fillSdf" : "carveSdf"),
        vec4FromLua(params),
        color,
        place,
        [&](IRMath::ivec3, std::size_t flat, bool shouldPlace, IRMath::Color fillColor) {
            if (flat >= records.size()) {
                return;
            }
            if (shouldPlace) {
                IRPrefab::Voxel::detail::placeVoxel(records[flat], fillColor);
            } else {
                records[flat].deactivate();
            }
        }
    );
}

inline void bindShapeTable(LuaScript &luaScript) {
    if (luaScript.lua()["IRShape"].valid()) {
        return;
    }
    sol::table shapes = luaScript.lua().create_table();
#define IR_BIND_SHAPE(name) shapes[#name] = static_cast<lua_Integer>(IRMath::SDF::ShapeType::name)
    IR_BIND_SHAPE(BOX);
    IR_BIND_SHAPE(SPHERE);
    IR_BIND_SHAPE(CYLINDER);
    IR_BIND_SHAPE(ELLIPSOID);
    IR_BIND_SHAPE(CURVED_PANEL);
    IR_BIND_SHAPE(WEDGE);
    IR_BIND_SHAPE(TAPERED_BOX);
    IR_BIND_SHAPE(CONE);
    IR_BIND_SHAPE(TORUS);
#undef IR_BIND_SHAPE
    luaScript.lua()["IRShape"] = shapes;
}

inline void bindVoxelAssetLoader(LuaScript &luaScript) {
    auto &lua = luaScript.lua();
    if (!lua["IRAsset"].valid()) {
        lua["IRAsset"] = lua.create_table();
    }
    lua["IRAsset"]["loadVoxelSet"] = [](const std::string &path,
                                        sol::optional<lua_Integer> anchorValue,
                                        sol::optional<lua_Integer> targetCanvasValue) {
        IRComponents::EntityAnchor anchor = IRComponents::EntityAnchor::CENTER;
        if (anchorValue) {
            if (*anchorValue < static_cast<lua_Integer>(IRComponents::EntityAnchor::kFirst) ||
                *anchorValue > static_cast<lua_Integer>(IRComponents::EntityAnchor::kLast)) {
                throw sol::error{
                    "IRAsset.loadVoxelSet: anchor must be an IRComponent.EntityAnchor value"
                };
            }
            anchor = static_cast<IRComponents::EntityAnchor>(*anchorValue);
        }
        const IREntity::EntityId targetCanvas =
            targetCanvasValue ? static_cast<IREntity::EntityId>(*targetCanvasValue)
                              : IREntity::kNullEntity;
        auto loaded = IRAsset::loadVoxelSet(path);
        if (!loaded.ok()) {
            throw sol::error{"IRAsset.loadVoxelSet: " + loaded.status_.message_};
        }
        if (loaded.value_.mode_ != IRAsset::VoxelSetMode::DENSE) {
            throw sol::error{"IRAsset.loadVoxelSet: asset is not DENSE"};
        }
        auto set = IRPrefab::DenseVoxel::toComponent(loaded.value_.dense_, anchor, targetCanvas);
        if (set.recordCount() == 0) {
            throw sol::error{"IRAsset.loadVoxelSet: DENSE payload is empty or malformed"};
        }
        return set;
    };
}

} // namespace detail

// The 3-arg Lua ctor takes an `EntityAnchor`; the C++ `bool` overload is
// deliberately not bound.
//
// Registering both would put a `bool` and an integer-backed enum in one sol2
// overload set, where a Lua boolean and a Lua integer are mutually
// convertible at the binding boundary — so which arm a 3-arg call binds
// depends on declaration order rather than on the value's type, and picking
// wrong is SILENT: `false` would construct anchor 0 and `true` anchor 1,
// which coincidentally match CORNER/CENTER, so the bug would surface only
// once a third anchor is passed as a boolean-ish value.
//
// The C++ `bool centerAroundOrigin` ctor stays for C++ callers; only the Lua
// surface is anchor-only.
//
// The **4-arg** form appends the ctor's `targetCanvas`, which selects the
// canvas whose pool the set allocates from instead of the *active* one. That
// is what makes the Lua surface reachable headlessly: the 2- and 3-arg forms
// route through the asserting `IRPrefab::VoxelPool::activeCanvasEntity()` and
// so need a live RenderManager, while a test that creates a `C_VoxelPool`
// canvas itself can pass that entity in and assert the placement a Lua caller
// actually gets (`test/script/lua_entity_anchor_test.cpp`) — the "silently
// binds the wrong arm" failure is only observable through a constructed set's
// baked positions.
//
// `lodMin` / `lodMax` are the set's LOD band as `IRRender.LodLevel` integers;
// GATE_VOXEL_SETS_BY_LOD applies a write on its next tick.
template <> inline void bindLuaType<IRComponents::C_VoxelSetNew>(LuaScript &luaScript) {
    auto voxelSetType = luaScript.registerType<
        IRComponents::C_VoxelSetNew,
        IRComponents::C_VoxelSetNew(
            IRMath::ivec3,
            IRMath::Color,
            IRComponents::EntityAnchor,
            IREntity::EntityId
        ),
        IRComponents::C_VoxelSetNew(IRMath::ivec3, IRMath::Color, IRComponents::EntityAnchor),
        IRComponents::C_VoxelSetNew(IRMath::ivec3, IRMath::Color)>(
        "C_VoxelSetNew",
        "lodMin",
        &IRComponents::C_VoxelSetNew::lodMin_,
        "lodMax",
        &IRComponents::C_VoxelSetNew::lodMax_
    );

    voxelSetType["setVoxel"] =
        [](IRComponents::C_VoxelSetNew &set, int x, int y, int z, IRMath::Color color) {
            detail::setVoxelRaw(set, x, y, z, color);
            set.resyncAfterRawEdits();
        };
    voxelSetType["clearVoxel"] = [](IRComponents::C_VoxelSetNew &set, int x, int y, int z) {
        detail::clearVoxelRaw(set, x, y, z);
        set.resyncAfterRawEdits();
    };
    voxelSetType["fillSdf"] = [](IRComponents::C_VoxelSetNew &set,
                                 lua_Integer shape,
                                 sol::table params,
                                 IRMath::Color color) {
        detail::applySdfRaw(set, shape, params, color, true);
        set.resyncAfterRawEdits();
    };
    voxelSetType["carveSdf"] =
        [](IRComponents::C_VoxelSetNew &set, lua_Integer shape, sol::table params) {
            detail::applySdfRaw(set, shape, params, IRMath::Color{}, false);
            set.resyncAfterRawEdits();
        };
    voxelSetType["batch"] = [&luaScript](
                                IRComponents::C_VoxelSetNew &set,
                                sol::protected_function fn
                            ) {
        auto state = std::make_shared<detail::VoxelBatchState>(set);
        sol::table batch = luaScript.lua().create_table();
        batch["setVoxel"] = [state](sol::table, int x, int y, int z, IRMath::Color color) {
            detail::setVoxelRaw(detail::requireActiveBatch(state), x, y, z, color);
        };
        batch["clearVoxel"] = [state](sol::table, int x, int y, int z) {
            detail::clearVoxelRaw(detail::requireActiveBatch(state), x, y, z);
        };
        batch["fillSdf"] =
            [state](sol::table, lua_Integer shape, sol::table params, IRMath::Color color) {
                detail::applySdfRaw(detail::requireActiveBatch(state), shape, params, color, true);
            };
        batch["carveSdf"] = [state](sol::table, lua_Integer shape, sol::table params) {
            detail::applySdfRaw(
                detail::requireActiveBatch(state),
                shape,
                params,
                IRMath::Color{},
                false
            );
        };

        sol::protected_function_result result = fn(batch);
        state->set_ = nullptr;
        set.resyncAfterRawEdits();
        if (!result.valid()) {
            sol::error error = result;
            throw sol::error{error.what()};
        }
    };

    detail::bindShapeTable(luaScript);
    detail::bindVoxelAssetLoader(luaScript);
}
} // namespace IRScript

#endif /* COMPONENT_VOXEL_SET_LUA_H */
