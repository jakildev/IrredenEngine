#ifndef COMPONENT_SHAPE_DESCRIPTOR_LUA_H
#define COMPONENT_SHAPE_DESCRIPTOR_LUA_H

#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_ShapeDescriptor> = true;

template <> inline void bindLuaType<IRComponents::C_ShapeDescriptor>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_ShapeDescriptor,
        IRComponents::C_ShapeDescriptor(IRRender::ShapeType, IRMath::vec4, IRMath::Color)>(
        "C_ShapeDescriptor",
        "shapeType", &IRComponents::C_ShapeDescriptor::shapeType_,
        "params", &IRComponents::C_ShapeDescriptor::params_,
        "color", &IRComponents::C_ShapeDescriptor::color_,
        "flags", &IRComponents::C_ShapeDescriptor::flags_,
        "lodLevel", &IRComponents::C_ShapeDescriptor::lodLevel_);
}
} // namespace IRScript

#endif /* COMPONENT_SHAPE_DESCRIPTOR_LUA_H */
