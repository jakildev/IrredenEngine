#ifndef LUA_VIEWPORT_BINDINGS_H
#define LUA_VIEWPORT_BINDINGS_H

// Lua surface for secondary viewports (`IRPrefab::Viewport`), installed on the
// `IRRender` table by `LuaScript::bindLuaViewport()`. Every entry is a thin
// forward; a viewport id is the number `createViewport` returned.

#include <irreden/render/viewport.hpp>
#include <irreden/script/ir_script_types.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <functional>
#include <stdexcept>
#include <string>

namespace IRScript::detail {

using ViewportFactory = std::function<IREntity::EntityId(const IRPrefab::Viewport::Desc &)>;

inline IREntity::EntityId createCanvasViewport(const IRPrefab::Viewport::Desc &desc) {
    return IRPrefab::Viewport::create(desc);
}

inline IREntity::EntityId requireViewport(lua_Integer id, const char *function) {
    const IREntity::EntityId viewport = static_cast<IREntity::EntityId>(id);
    if (id <= 0 || !IRPrefab::Viewport::isViewport(viewport)) {
        throw std::invalid_argument(
            std::string("IRRender.") + function + " argument 1 is not a live viewport id"
        );
    }
    return viewport;
}

// An entity argument: a LuaEntity or a positive entity-id number naming a live
// entity. nil is kNullEntity, which clears a subject.
inline IREntity::EntityId optionalViewportEntity(sol::object value, const char *function) {
    if (!value.valid() || value.get_type() == sol::type::lua_nil) {
        return IREntity::kNullEntity;
    }
    IREntity::EntityId entity = IREntity::kNullEntity;
    if (value.is<IRScript::LuaEntity>()) {
        entity = value.as<IRScript::LuaEntity>().entity;
    } else if (value.get_type() == sol::type::number) {
        const lua_Integer id = value.as<lua_Integer>();
        entity = id > 0 ? static_cast<IREntity::EntityId>(id) : IREntity::kNullEntity;
    }
    if (entity == IREntity::kNullEntity || !IREntity::entityExists(entity)) {
        throw std::invalid_argument(
            std::string("IRRender.") + function + " argument 2 must be a live entity or nil"
        );
    }
    return entity;
}

/// @p createViewport stands up the viewport for a `createViewport` call; the
/// default makes a GPU canvas.
inline void bindViewport(LuaScript &script, ViewportFactory createViewport = createCanvasViewport) {
    sol::state &lua = script.lua();
    sol::object existing = lua["IRRender"];
    if (existing.valid() && existing.get_type() != sol::type::lua_nil &&
        existing.get_type() != sol::type::table) {
        throw std::invalid_argument("IRRender must be a table before bindLuaViewport()");
    }
    sol::table render =
        existing.get_type() == sol::type::table ? existing.as<sol::table>() : lua.create_table();

    render["createViewport"] = [createViewport](sol::table options) -> lua_Integer {
        IRPrefab::Viewport::Desc desc{};
        desc.rectOrigin_ = IRMath::ivec2(options.get_or("x", 0), options.get_or("y", 0));
        desc.rectSize_ = IRMath::ivec2(
            options.get_or("width", desc.rectSize_.x),
            options.get_or("height", desc.rectSize_.y)
        );
        desc.zoom_ = options.get_or("zoom", desc.zoom_);
        desc.yawRadians_ = options.get_or("yaw", desc.yawRadians_);
        sol::object focus = options["focus"];
        if (focus.valid() && focus.get_type() != sol::type::lua_nil) {
            desc.focus_ = vec3FromLua(focus);
        }
        return static_cast<lua_Integer>(createViewport(desc));
    };
    render["setViewportRect"] = [](lua_Integer id, int x, int y, int width, int height) {
        IRPrefab::Viewport::setRect(
            requireViewport(id, "setViewportRect"),
            IRMath::ivec2(x, y),
            IRMath::ivec2(width, height)
        );
    };
    render["setViewportCamera"] = [](lua_Integer id, float zoom, float yawRadians) {
        IRPrefab::Viewport::setCamera(requireViewport(id, "setViewportCamera"), zoom, yawRadians);
    };
    render["setViewportFocus"] = [](lua_Integer id, float x, float y, float z) {
        IRPrefab::Viewport::setFocus(
            requireViewport(id, "setViewportFocus"),
            IRMath::vec3(x, y, z)
        );
    };
    render["setViewportVisible"] = [](lua_Integer id, bool visible) {
        IRPrefab::Viewport::setVisible(requireViewport(id, "setViewportVisible"), visible);
    };
    render["setViewportSubject"] = [](lua_Integer id, sol::object subject) {
        const IREntity::EntityId viewport = requireViewport(id, "setViewportSubject");
        IRPrefab::Viewport::setSubject(
            viewport,
            optionalViewportEntity(subject, "setViewportSubject")
        );
    };
    render["getViewportSubject"] = [](lua_Integer id) -> sol::optional<lua_Integer> {
        const IREntity::EntityId subject =
            IRPrefab::Viewport::drawnSubject(requireViewport(id, "getViewportSubject"));
        if (subject == IREntity::kNullEntity) {
            return sol::nullopt;
        }
        return static_cast<lua_Integer>(subject);
    };
    render["destroyViewport"] = [](lua_Integer id) {
        IRPrefab::Viewport::destroy(requireViewport(id, "destroyViewport"));
    };
    lua["IRRender"] = render;
}

} // namespace IRScript::detail

#endif /* LUA_VIEWPORT_BINDINGS_H */
